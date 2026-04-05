`timescale 1ns / 1ps


module tb_fifo;

    localparam integer N_DATA    = 64;
    localparam integer DATA_WIDTH = 32;

    localparam integer MAX_STALLS = 16;


    // -------------------------------------------------------------------------
    // Buffers
    // -------------------------------------------------------------------------
    reg [DATA_WIDTH-1:0] send_buff [];
    reg [DATA_WIDTH-1:0] recv_buff [];

    // -------------------------------------------------------------------------
    // DUT port declarations
    // -------------------------------------------------------------------------
    reg  aclk    = 0;
    reg  aresetn;

    reg  [DATA_WIDTH-1:0] s_axis_tdata;
    reg                   s_axis_tlast;
    reg                   s_axis_tvalid;
    wire                  s_axis_tready;

    wire                  m_axis_tvalid;
    wire [DATA_WIDTH-1:0] m_axis_tdata;
    wire                  m_axis_tlast;
    reg                   m_axis_tready;

    // -------------------------------------------------------------------------
    // DUT instantiation
    // -------------------------------------------------------------------------
    project_ip #(
        .N(32),
        .DATA_WIDTH(DATA_WIDTH)
    ) dut (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tdata(s_axis_tdata),
        .s_axis_tlast(s_axis_tlast),
        .m_axis_tvalid(m_axis_tvalid),
        .m_axis_tready(m_axis_tready),
        .m_axis_tdata(m_axis_tdata),
        .m_axis_tlast(m_axis_tlast)
    );

    // -------------------------------------------------------------------------
    // Clock  – 100 ns period
    // -------------------------------------------------------------------------
    always #50 aclk = ~aclk;

    // -------------------------------------------------------------------------
    // tlast edge detector – recv loop exits one cycle after tlast goes high
    // -------------------------------------------------------------------------
    reg m_axis_tlast_prev = 1'b0;
    always @(posedge aclk) m_axis_tlast_prev <= m_axis_tlast;

    integer recv_cnt;

    // -------------------------------------------------------------------------
    // Buffer allocation
    // -------------------------------------------------------------------------
    initial begin
        send_buff = new[N_DATA];
        recv_buff = new[N_DATA];
    end

    // -------------------------------------------------------------------------
    // Task: send
    //   Drives n_words words from send_buff[] onto S_AXIS.
    //   tlast is asserted on the last word (wi == n_words-1).
    //   Supports up to MAX_STALLS pause points (n_stalls=0 → no pauses).
    // -------------------------------------------------------------------------
    task automatic send;
        input integer n_words;
        input integer n_stalls;
        input integer stall_words [0:MAX_STALLS-1];
        input integer stall_cycs  [0:MAX_STALLS-1];
        integer wi, si;
        begin
            wi = 0;
            si = 0;

            @(posedge aclk)
            begin
                s_axis_tvalid <= 1'b1;
                s_axis_tdata  <= send_buff[wi];
                wi = wi + 1;
            end

            while (wi < n_words)
            begin
                @(posedge aclk)
                begin
                    if (s_axis_tvalid & s_axis_tready)
                    begin
                        s_axis_tdata  <= send_buff[wi];
                        if (wi == n_words - 1)
                            s_axis_tlast <= 1'b1;
                        else if ((si < n_stalls) & (stall_words[si] == wi))
                        begin
                            s_axis_tvalid <= 1'b0;
                            repeat (stall_cycs[si] - 1) @(posedge aclk);
                            @(posedge aclk) s_axis_tvalid <= 1'b1;
                            si = si + 1;
                        end
                        wi = wi + 1;
                    end
                end
            end

            do
            begin
                @(posedge aclk)
                begin
                    if (s_axis_tvalid & s_axis_tready)
                    begin
                        s_axis_tvalid <= 1'b0;
                        s_axis_tlast  <= 1'b0;
                    end
                end
            end
            while (~(s_axis_tvalid & s_axis_tready));
        end
    endtask

    // -------------------------------------------------------------------------
    // Task: recv
    //   Captures M_AXIS words into recv_buff[] until tlast is seen.
    //   recv_cnt holds the total number of words received.
    //   Supports up to MAX_STALLS back-pressure points.
    // -------------------------------------------------------------------------
    task automatic recv;
        input integer n_stalls;
        input integer stall_words [0:MAX_STALLS-1];
        input integer stall_cycs  [0:MAX_STALLS-1];
        integer si;
        begin
            recv_cnt = 0;
            si       = 0;

            @(posedge aclk) m_axis_tready <= 1'b1;

            while (m_axis_tlast | ~m_axis_tlast_prev)
            begin
                @(posedge aclk)
                begin
                    if (m_axis_tvalid & m_axis_tready)
                    begin
                        recv_buff[recv_cnt] = m_axis_tdata;
                        if (m_axis_tlast)
                            m_axis_tready <= 1'b0;
                        else if ((si < n_stalls) & (stall_words[si] == recv_cnt))
                        begin
                            m_axis_tready <= 1'b0;
                            repeat (stall_cycs[si] - 1) @(posedge aclk);
                            @(posedge aclk) m_axis_tready <= 1'b1;
                            si = si + 1;
                        end
                        recv_cnt = recv_cnt + 1;
                    end
                end
            end
        end
    endtask

    // =========================================================================
    // Stimulus
    // =========================================================================
    initial
    begin : STIM
        integer i;

        integer no_stall_w [0:MAX_STALLS-1];
        integer no_stall_c [0:MAX_STALLS-1];

        integer fp_stall_w [0:MAX_STALLS-1];
        integer fp_stall_c [0:MAX_STALLS-1];

        integer bp_stall_w [0:MAX_STALLS-1];
        integer bp_stall_c [0:MAX_STALLS-1];

        integer sim_s_stall_w [0:MAX_STALLS-1];
        integer sim_s_stall_c [0:MAX_STALLS-1];
        integer sim_r_stall_w [0:MAX_STALLS-1];
        integer sim_r_stall_c [0:MAX_STALLS-1];

        // ---- zero-initialise all descriptor arrays -----------------------
        for (i = 0; i < MAX_STALLS; i = i + 1) begin
            no_stall_w[i]   = 0; no_stall_c[i]   = 0;
            fp_stall_w[i]   = 0; fp_stall_c[i]   = 0;
            bp_stall_w[i]   = 0; bp_stall_c[i]   = 0;
            sim_s_stall_w[i]= 0; sim_s_stall_c[i]= 0;
            sim_r_stall_w[i]= 0; sim_r_stall_c[i]= 0;
        end

        // ---- front-pressure: stall right after word 0 (first), mid-burst,
        //                      and right before the last word ---------------
        fp_stall_w[0] =  1;          fp_stall_c[0] =  3;  // edge: just after first
        fp_stall_w[1] =  8;          fp_stall_c[1] =  5;
        fp_stall_w[2] = 16;          fp_stall_c[2] =  4;
        fp_stall_w[3] = 32;          fp_stall_c[3] =  7;
        fp_stall_w[4] = N_DATA - 2;  fp_stall_c[4] =  3;  // edge: one before last
        fp_stall_w[5] = N_DATA - 1;  fp_stall_c[5] =  6;  // edge: on last word

        // ---- back-pressure: stall right after word 0 (first), mid-burst,
        //                     and right before/on the last word -------------
        bp_stall_w[0] =  1;          bp_stall_c[0] =  3;  // edge: just after first
        bp_stall_w[1] =  8;          bp_stall_c[1] =  5;
        bp_stall_w[2] = 16;          bp_stall_c[2] =  8;
        bp_stall_w[3] = 32;          bp_stall_c[3] =  4;
        bp_stall_w[4] = N_DATA - 2;  bp_stall_c[4] =  3;  // edge: one before last
        bp_stall_w[5] = N_DATA - 1;  bp_stall_c[5] =  6;  // edge: on last word

        // ---- simultaneous: sender and receiver stall at different points,
        //                    including overlapping windows -------------------
        sim_s_stall_w[0] =  1;          sim_s_stall_c[0] = 3;   // edge: just after first
        sim_s_stall_w[1] = 12;          sim_s_stall_c[1] = 5;
        sim_s_stall_w[2] = 32;          sim_s_stall_c[2] = 4;
        sim_s_stall_w[3] = N_DATA - 1;  sim_s_stall_c[3] = 3;   // edge: on last word

        sim_r_stall_w[0] =  1;          sim_r_stall_c[0] = 5;   // edge: overlaps with sender
        sim_r_stall_w[1] = 16;          sim_r_stall_c[1] = 6;
        sim_r_stall_w[2] = 32;          sim_r_stall_c[2] = 7;   // edge: same word as sender
        sim_r_stall_w[3] = N_DATA - 2;  sim_r_stall_c[3] = 4;   // edge: one before last

        // ------------------------------------------------------------------
        // Reset
        // ------------------------------------------------------------------
        aresetn       = 1'b0;
        s_axis_tvalid = 1'b0;
        s_axis_tlast  = 1'b0;
        s_axis_tdata  = '0;
        m_axis_tready = 1'b0;

        repeat (4) @(posedge aclk);
        aresetn = 1'b1;
        repeat (2) @(posedge aclk);

        // ==================================================================
        // TEST 1 – Clean (no front / back pressure), incremental data
        // ==================================================================
        $display("=== TEST 1: Clean (no stalls), incremental data ===");

        for (i = 0; i < N_DATA; i = i + 1)
            send_buff[i] = i;                          // 0x00, 0x01, 0x02 ...

        fork
            send(N_DATA, 0, no_stall_w, no_stall_c);
            recv(         0, no_stall_w, no_stall_c);
        join

        for (i = 0; i < recv_cnt; i = i + 1)
            if (recv_buff[i] !== send_buff[i])
                $display("  MISMATCH word %0d : sent %08h  got %08h", i, send_buff[i], recv_buff[i]);
        if (recv_cnt !== N_DATA)
            $display("  COUNT MISMATCH : expected %0d  got %0d", N_DATA, recv_cnt);
        else
            $display("  PASS (%0d words)", recv_cnt);

        repeat (4) @(posedge aclk);

        // ==================================================================
        // TEST 2 – Front pressure only, incremental data
        //          Stalls: after word 0, words 8/16/32, and at last-1/last
        // ==================================================================
        $display("=== TEST 2: Front pressure only ===");

        for (i = 0; i < N_DATA; i = i + 1)
            send_buff[i] = i;

        fork
            send(N_DATA, 6, fp_stall_w, fp_stall_c);
            recv(         0, no_stall_w, no_stall_c);
        join

        for (i = 0; i < recv_cnt; i = i + 1)
            if (recv_buff[i] !== send_buff[i])
                $display("  MISMATCH word %0d : sent %08h  got %08h", i, send_buff[i], recv_buff[i]);
        if (recv_cnt !== N_DATA)
            $display("  COUNT MISMATCH : expected %0d  got %0d", N_DATA, recv_cnt);
        else
            $display("  PASS (%0d words)", recv_cnt);

        repeat (4) @(posedge aclk);

        // ==================================================================
        // TEST 3 – Back pressure only, incremental data
        //          Stalls: after word 0, words 8/16/32, and at last-1/last
        // ==================================================================
        $display("=== TEST 3: Back pressure only ===");

        for (i = 0; i < N_DATA; i = i + 1)
            send_buff[i] = i;

        fork
            send(N_DATA, 0, no_stall_w, no_stall_c);
            recv(         6, bp_stall_w, bp_stall_c);
        join

        for (i = 0; i < recv_cnt; i = i + 1)
            if (recv_buff[i] !== send_buff[i])
                $display("  MISMATCH word %0d : sent %08h  got %08h", i, send_buff[i], recv_buff[i]);
        if (recv_cnt !== N_DATA)
            $display("  COUNT MISMATCH : expected %0d  got %0d", N_DATA, recv_cnt);
        else
            $display("  PASS (%0d words)", recv_cnt);

        repeat (4) @(posedge aclk);

        // ==================================================================
        // TEST 4 – Simultaneous front + back pressure, incremental data
        //          Both sides stall, including same-word and overlapping
        //          windows to stress arbitration and pointer logic
        // ==================================================================
        $display("=== TEST 4: Simultaneous front + back pressure ===");

        for (i = 0; i < N_DATA; i = i + 1)
            send_buff[i] = i;

        fork
            send(N_DATA, 4, sim_s_stall_w, sim_s_stall_c);
            recv(         4, sim_r_stall_w, sim_r_stall_c);
        join

        for (i = 0; i < recv_cnt; i = i + 1)
            if (recv_buff[i] !== send_buff[i])
                $display("  MISMATCH word %0d : sent %08h  got %08h", i, send_buff[i], recv_buff[i]);
        if (recv_cnt !== N_DATA)
            $display("  COUNT MISMATCH : expected %0d  got %0d", N_DATA, recv_cnt);
        else
            $display("  PASS (%0d words)", recv_cnt);

        repeat (4) @(posedge aclk);

        // ==================================================================
        $display("=== All tests complete ===");
        $finish;
    end

endmodule

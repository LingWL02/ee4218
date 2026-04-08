`timescale 1ns / 1ps

// =============================================================================
//  tb_project_ip.sv
//
//  AXI-Stream testbench for project_ip (2-layer MLP accelerator).
//  Reads weights, features, and labels directly from CSV files at runtime
//  using $fopen / $fscanf — no intermediate .mem files required.
//
//  CSV layout expected (no header row):
//    w_hid.csv   - 8 rows × 2 comma-separated cols  (weights + bias, hidden)
//    w_out.csv   - 3 rows × 1 col                   (weights + bias, output)
//    X.csv       - 64 rows × 7 comma-separated cols  (input features)
//    labels.csv  - 64 rows × 1 col                   (ground-truth class 0/1)
//
//  AXI-S stream layout sent over S_AXIS (one 32-bit word per beat):
//    [0  .. 15] : w_hid interleaved - beat 2i → col0, beat 2i+1 → col1
//    [16 .. 18] : w_out[0], w_out[1], w_out[2]
//    [19 .. 466]: X rows, 7 features each; tlast on the very last beat
//
//  M_AXIS output : one word per input row (predicted class 0 or 1),
//                  tlast propagated from the last input tlast.
//
//  Tests (reuse send/recv tasks from the FIFO testbench template):
//    TEST 1 - no stalls
//    TEST 2 - front-pressure  (S_AXIS stalls)
//    TEST 3 - back-pressure   (M_AXIS stalls)
//    TEST 4 - simultaneous front + back pressure
//    TEST 5 - early tlast inside w_hid phase   → INVALID_TOKEN expected
//    TEST 6 - early tlast mid-row in X phase   → INVALID_TOKEN expected
// =============================================================================

module tb_project_ip;

    // -------------------------------------------------------------------------
    // Parameters - must match project_ip defaults
    // -------------------------------------------------------------------------
    localparam integer N_W_HIDDEN = 16;   // 8 rows × 2 cols interleaved
    localparam integer N_W_OUTPUT = 3;    // 2 weights + 1 bias
    localparam integer N_X        = 7;    // features per row
    localparam integer DATA_WIDTH = 32;

    localparam integer N_ROWS = 64;
    // Total beats sent: 16 (w_hid) + 3 (w_out) + 64×7 (X) = 467
    localparam integer N_SEND = N_W_HIDDEN + N_W_OUTPUT + N_ROWS * N_X;
    localparam integer N_RECV = N_ROWS;   // one output word per row

    localparam integer MAX_STALLS = 16;

    // -------------------------------------------------------------------------
    // Data buffers (populated from CSV by load_csv task)
    // -------------------------------------------------------------------------
    reg [DATA_WIDTH-1:0] send_buff  [0:N_SEND-1];
    reg [DATA_WIDTH-1:0] label_buff [0:N_RECV-1];
    reg [DATA_WIDTH-1:0] recv_buff  [0:N_RECV-1];

    // -------------------------------------------------------------------------
    // DUT ports
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
    // DUT
    // -------------------------------------------------------------------------
    project_ip #(
        .N_W_HIDDEN (N_W_HIDDEN),
        .N_W_OUTPUT (N_W_OUTPUT),
        .N_X        (N_X),
        .DATA_WIDTH (DATA_WIDTH)
    ) dut (
        .aclk         (aclk),
        .aresetn      (aresetn),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tdata (s_axis_tdata),
        .s_axis_tlast (s_axis_tlast),
        .m_axis_tvalid(m_axis_tvalid),
        .m_axis_tready(m_axis_tready),
        .m_axis_tdata (m_axis_tdata),
        .m_axis_tlast (m_axis_tlast)
    );

    // -------------------------------------------------------------------------
    // Clock - 100 ns period
    // -------------------------------------------------------------------------
    always #50 aclk = ~aclk;

    // -------------------------------------------------------------------------
    // tlast edge detector - recv exits one cycle after the last handshake
    // -------------------------------------------------------------------------
    reg m_axis_tlast_prev = 1'b0;
    always @(posedge aclk) m_axis_tlast_prev <= m_axis_tlast;

    integer recv_cnt;

    // =========================================================================
    //  Task : load_csv
    //
    //  Opens each CSV file with $fopen and reads all values into:
    //    send_buff[]  - full AXI-S payload (w_hid interleaved, w_out, X rows)
    //    label_buff[] - expected predicted class per row (from labels.csv)
    // =========================================================================
    task automatic load_csv;
        integer fd;
        integer r;
        integer val0, val1;
        integer base;
        begin
            // -----------------------------------------------------------------
            // w_hid.csv : 8 rows, 2 comma-separated columns
            //   col0 → even beats (wh0 RAM), col1 → odd beats (wh1 RAM)
            // -----------------------------------------------------------------
            fd = $fopen("w_hid.csv", "r");
            if (fd == 0) begin
                $display("ERROR: cannot open w_hid.csv"); $finish;
            end
            for (r = 0; r < 8; r = r + 1) begin
                if ($fscanf(fd, "%d,%d\n", val0, val1) != 2) begin
                    $display("ERROR: w_hid.csv - bad data at row %0d", r); $finish;
                end
                send_buff[2*r]   = val0;   // col0 → wh0
                send_buff[2*r+1] = val1;   // col1 → wh1
            end
            $fclose(fd);

            // -----------------------------------------------------------------
            // w_out.csv : 3 rows, 1 column
            // -----------------------------------------------------------------
            fd = $fopen("w_out.csv", "r");
            if (fd == 0) begin
                $display("ERROR: cannot open w_out.csv"); $finish;
            end
            for (r = 0; r < 3; r = r + 1) begin
                if ($fscanf(fd, "%d\n", val0) != 1) begin
                    $display("ERROR: w_out.csv - bad data at row %0d", r); $finish;
                end
                send_buff[N_W_HIDDEN + r] = val0;
            end
            $fclose(fd);

            // -----------------------------------------------------------------
            // X.csv : N_ROWS rows, N_X (7) comma-separated columns
            // -----------------------------------------------------------------
            fd = $fopen("X.csv", "r");
            if (fd == 0) begin
                $display("ERROR: cannot open X.csv"); $finish;
            end
            for (r = 0; r < N_ROWS; r = r + 1) begin
                base = N_W_HIDDEN + N_W_OUTPUT + r * N_X;
                if ($fscanf(fd, "%d,%d,%d,%d,%d,%d,%d\n",
                        send_buff[base+0], send_buff[base+1],
                        send_buff[base+2], send_buff[base+3],
                        send_buff[base+4], send_buff[base+5],
                        send_buff[base+6]) != N_X) begin
                    $display("ERROR: X.csv - bad data at row %0d", r); $finish;
                end
            end
            $fclose(fd);

            // -----------------------------------------------------------------
            // labels.csv : N_ROWS rows, 1 column (ground-truth class)
            // -----------------------------------------------------------------
            fd = $fopen("labels.csv", "r");
            if (fd == 0) begin
                $display("ERROR: cannot open labels.csv"); $finish;
            end
            for (r = 0; r < N_ROWS; r = r + 1) begin
                if ($fscanf(fd, "%d\n", label_buff[r]) != 1) begin
                    $display("ERROR: labels.csv - bad data at row %0d", r); $finish;
                end
            end
            $fclose(fd);

            $display("CSVs loaded: %0d send words, %0d labels", N_SEND, N_RECV);
        end
    endtask

    // =========================================================================
    //  Task : send
    //
    //  Drives n_words words from send_buff[] onto S_AXIS.
    //  tlast is asserted only on word index (n_words - 1).
    //  n_stalls non-zero entries in stall_words/stall_cycs insert
    //  front-pressure gaps (valid de-asserted for stall_cycs[si] cycles).
    // =========================================================================
    task automatic send;
        input integer n_words;
        input integer n_stalls;
        input integer stall_words [0:MAX_STALLS-1];
        input integer stall_cycs  [0:MAX_STALLS-1];
        integer wi, si;
        begin
            wi = 0;
            si = 0;

            // Present first word
            @(posedge aclk) begin
                s_axis_tvalid <= 1'b1;
                s_axis_tdata  <= send_buff[wi];
                s_axis_tlast  <= (n_words == 1) ? 1'b1 : 1'b0;
                wi = wi + 1;
            end

            while (wi < n_words) begin
                @(posedge aclk) begin
                    if (s_axis_tvalid & s_axis_tready) begin
                        s_axis_tdata <= send_buff[wi];
                        s_axis_tlast <= (wi == n_words - 1) ? 1'b1 : 1'b0;

                        if ((si < n_stalls) && (stall_words[si] == wi)) begin
                            // Insert a front-pressure gap
                            s_axis_tvalid <= 1'b0;
                            s_axis_tlast  <= 1'b0;
                            repeat (stall_cycs[si] - 1) @(posedge aclk);
                            @(posedge aclk) begin
                                s_axis_tvalid <= 1'b1;
                                s_axis_tdata  <= send_buff[wi];
                                s_axis_tlast  <= (wi == n_words - 1) ? 1'b1 : 1'b0;
                            end
                            si = si + 1;
                        end

                        wi = wi + 1;
                    end
                end
            end

            // Wait for last-word handshake, then de-assert
            do begin
                @(posedge aclk) begin
                    if (s_axis_tvalid & s_axis_tready) begin
                        s_axis_tvalid <= 1'b0;
                        s_axis_tlast  <= 1'b0;
                    end
                end
            end while (~(s_axis_tvalid & s_axis_tready));
        end
    endtask

    // =========================================================================
    //  Task : recv
    //
    //  Captures M_AXIS words into recv_buff[] until tlast is seen.
    //  recv_cnt holds the number of words captured after the task returns.
    //  n_stalls non-zero entries in stall_words/stall_cycs insert
    //  back-pressure gaps (ready de-asserted for stall_cycs[si] cycles).
    // =========================================================================
    task automatic recv;
        input integer n_stalls;
        input integer stall_words [0:MAX_STALLS-1];
        input integer stall_cycs  [0:MAX_STALLS-1];
        integer si;
        begin
            recv_cnt = 0;
            si       = 0;

            @(posedge aclk) m_axis_tready <= 1'b1;

            while (~m_axis_tlast_prev) begin
                @(posedge aclk) begin
                    if (m_axis_tvalid & m_axis_tready) begin
                        recv_buff[recv_cnt] = m_axis_tdata;

                        if (m_axis_tlast) begin
                            m_axis_tready <= 1'b0;
                        end else if ((si < n_stalls) && (stall_words[si] == recv_cnt)) begin
                            // Insert a back-pressure gap
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
    //  Task : check_results
    //
    //  Compares recv_buff[0..recv_cnt-1] against label_buff[].
    //  Prints per-row mismatches and a PASS/FAIL line.
    //  Returns pass=1 only when count and all values match.
    // =========================================================================
    task automatic check_results;
        output integer pass;
        integer i, mismatches;
        begin
            mismatches = 0;

            if (recv_cnt !== N_RECV) begin
                $display("  COUNT MISMATCH : expected %0d rows, got %0d",
                         N_RECV, recv_cnt);
                mismatches = mismatches + 1;
            end

            for (i = 0; i < recv_cnt && i < N_RECV; i = i + 1) begin
                if (recv_buff[i] !== label_buff[i]) begin
                    $display("  MISMATCH row %0d : label=%0d  DUT output=0x%08H",
                             i, label_buff[i], recv_buff[i]);
                    mismatches = mismatches + 1;
                end
            end

            if (mismatches == 0)
                $display("  PASS  (%0d / %0d rows matched label)", recv_cnt, N_RECV);
            else
                $display("  FAIL  (%0d mismatch(es))", mismatches);

            pass = (mismatches == 0) ? 1 : 0;
        end
    endtask

    // =========================================================================
    //  Task : reset_dut
    // =========================================================================
    task automatic reset_dut;
        begin
            aresetn       = 1'b0;
            s_axis_tvalid = 1'b0;
            s_axis_tlast  = 1'b0;
            s_axis_tdata  = '0;
            m_axis_tready = 1'b0;
            repeat (4) @(posedge aclk);
            aresetn = 1'b1;
            repeat (2) @(posedge aclk);
        end
    endtask

    // =========================================================================
    //  Stimulus
    // =========================================================================
    initial begin : STIM
        integer i;
        integer pass;
        integer total_pass;

        integer no_stall_w    [0:MAX_STALLS-1];
        integer no_stall_c    [0:MAX_STALLS-1];
        integer fp_stall_w    [0:MAX_STALLS-1];
        integer fp_stall_c    [0:MAX_STALLS-1];
        integer bp_stall_w    [0:MAX_STALLS-1];
        integer bp_stall_c    [0:MAX_STALLS-1];
        integer sim_s_stall_w [0:MAX_STALLS-1];
        integer sim_s_stall_c [0:MAX_STALLS-1];
        integer sim_r_stall_w [0:MAX_STALLS-1];
        integer sim_r_stall_c [0:MAX_STALLS-1];

        total_pass = 0;

        // Zero-initialise all stall descriptor arrays
        for (i = 0; i < MAX_STALLS; i = i + 1) begin
            no_stall_w[i]    = 0; no_stall_c[i]    = 0;
            fp_stall_w[i]    = 0; fp_stall_c[i]    = 0;
            bp_stall_w[i]    = 0; bp_stall_c[i]    = 0;
            sim_s_stall_w[i] = 0; sim_s_stall_c[i] = 0;
            sim_r_stall_w[i] = 0; sim_r_stall_c[i] = 0;
        end

        // Front-pressure: stalls spanning all three send phases
        fp_stall_w[0] =   1;        fp_stall_c[0] = 3; // inside w_hid
        fp_stall_w[1] =  15;        fp_stall_c[1] = 4; // last beat of w_hid
        fp_stall_w[2] =  17;        fp_stall_c[2] = 5; // inside w_out
        fp_stall_w[3] =  19;        fp_stall_c[3] = 3; // first X feature
        fp_stall_w[4] = 100;        fp_stall_c[4] = 6; // mid-stream row
        fp_stall_w[5] = N_SEND - 2; fp_stall_c[5] = 4; // penultimate beat

        // Back-pressure: stalls spread across the 64 output words
        bp_stall_w[0] =  0;         bp_stall_c[0] = 3; // first output
        bp_stall_w[1] =  8;         bp_stall_c[1] = 5;
        bp_stall_w[2] = 20;         bp_stall_c[2] = 7;
        bp_stall_w[3] = 40;         bp_stall_c[3] = 4;
        bp_stall_w[4] = N_RECV - 2; bp_stall_c[4] = 3;
        bp_stall_w[5] = N_RECV - 1; bp_stall_c[5] = 5; // last output

        // Simultaneous: overlapping send + recv stall windows
        sim_s_stall_w[0] =   5;        sim_s_stall_c[0] = 4;
        sim_s_stall_w[1] =  50;        sim_s_stall_c[1] = 6;
        sim_s_stall_w[2] = 200;        sim_s_stall_c[2] = 3;
        sim_s_stall_w[3] = N_SEND - 2; sim_s_stall_c[3] = 5;

        sim_r_stall_w[0] =  0;         sim_r_stall_c[0] = 5; // overlaps sender
        sim_r_stall_w[1] = 10;         sim_r_stall_c[1] = 3;
        sim_r_stall_w[2] = 30;         sim_r_stall_c[2] = 7; // same beat as sender
        sim_r_stall_w[3] = N_RECV - 2; sim_r_stall_c[3] = 4;

        // Load all CSVs into buffers
        load_csv();
        reset_dut();
        $display("");

        // =====================================================================
        // TEST 1 - Full inference, no stalls
        // =====================================================================
        $display("=== TEST 1: Full inference, no stalls ===");
        fork
            send(N_SEND, 0, no_stall_w, no_stall_c);
            recv(        0, no_stall_w, no_stall_c);
        join
        check_results(pass);
        total_pass = total_pass + pass;
        repeat (4) @(posedge aclk);

        // =====================================================================
        // TEST 2 - Front-pressure only (S_AXIS stalls)
        // =====================================================================
        $display("=== TEST 2: Full inference, front-pressure (S_AXIS stalls) ===");
        fork
            send(N_SEND, 6, fp_stall_w, fp_stall_c);
            recv(        0, no_stall_w, no_stall_c);
        join
        check_results(pass);
        total_pass = total_pass + pass;
        repeat (4) @(posedge aclk);

        // =====================================================================
        // TEST 3 - Back-pressure only (M_AXIS stalls)
        // =====================================================================
        $display("=== TEST 3: Full inference, back-pressure (M_AXIS stalls) ===");
        fork
            send(N_SEND, 0, no_stall_w, no_stall_c);
            recv(        6, bp_stall_w, bp_stall_c);
        join
        check_results(pass);
        total_pass = total_pass + pass;
        repeat (4) @(posedge aclk);

        // =====================================================================
        // TEST 4 - Simultaneous front + back pressure
        // =====================================================================
        $display("=== TEST 4: Full inference, simultaneous front + back pressure ===");
        fork
            send(N_SEND, 4, sim_s_stall_w, sim_s_stall_c);
            recv(        4, sim_r_stall_w,  sim_r_stall_c);
        join
        check_results(pass);
        total_pass = total_pass + pass;
        repeat (4) @(posedge aclk);

        // =====================================================================
        // TEST 5, 6, 7, 8 - Invalid tlast assertions → INVALID_TOKEN expected
        //   Send only 8 of 16 w_hid words then assert tlast prematurely.
        // =====================================================================
        $display("=== TEST 5: Early tlast in w_hid phase (error path) ===");
        begin : EARLY_TLAST_WHID
        fork
            send(6, 0, sim_s_stall_w, sim_s_stall_c);
            recv(        0, sim_r_stall_w,  sim_r_stall_c);
        join
        if (recv_cnt !== 1)
            $display("  COUNT MISMATCH : expected %0d rows, got %0d", 1, recv_cnt);
        else if (recv_buff[0] === 32'hDEADBEEF)
            $display("  PASS  - INVALID_TOKEN (0xDEADBEEF) received as expected");
        else
            $display("  FAIL  - got 0x%08H, expected 0xDEADBEEF", recv_buff[0]);
        repeat (4) @(posedge aclk);
        end

        $display("=== TEST 6: Early tlast in w_out phase (error path) ===");
        begin : EARLY_TLAST_WOUT
        fork
            send(N_W_HIDDEN + 1, 0, sim_s_stall_w, sim_s_stall_c);
            recv(                0, sim_r_stall_w,  sim_r_stall_c);
        join
        if (recv_cnt !== 1)
            $display("  COUNT MISMATCH : expected %0d rows, got %0d", 1, recv_cnt);
        else if (recv_buff[0] === 32'hDEADBEEF)
            $display("  PASS  - INVALID_TOKEN (0xDEADBEEF) received as expected");
        else
            $display("  FAIL  - got 0x%08H, expected 0xDEADBEEF", recv_buff[0]);
        repeat (4) @(posedge aclk);
        end

        $display("=== TEST 7: Early tlast in x phase (error path) ===");
        begin : EARLY_TLAST_X
        fork
            send(N_W_HIDDEN + N_W_OUTPUT + 4, 0, sim_s_stall_w, sim_s_stall_c);
            recv(                             0, sim_r_stall_w,  sim_r_stall_c);
        join
        if (recv_cnt !== 1)
            $display("  COUNT MISMATCH : expected %0d rows, got %0d", 1, recv_cnt);
        else if (recv_buff[0] === 32'hDEADBEEF)
            $display("  PASS  - INVALID_TOKEN (0xDEADBEEF) received as expected");
        else
            $display("  FAIL  - got 0x%08H, expected 0xDEADBEEF", recv_buff[0]);
        repeat (4) @(posedge aclk);
        end

        $display("=== TEST 8: Early tlast in x phase (error path) ===");
        begin : INVALID_TLAST_X
        fork
            send(N_W_HIDDEN + N_W_OUTPUT + N_X + 5, 0, sim_s_stall_w, sim_s_stall_c);
            recv(                                0, sim_r_stall_w,  sim_r_stall_c);
        join
        if (recv_cnt !== 2)
            $display("  COUNT MISMATCH : expected %0d rows, got %0d", 1, recv_cnt);
        else if ((recv_buff[0] === label_buff[0]) & (recv_buff[1] === 32'hDEADBEEF))
            $display("  PASS  - INVALID_TOKEN (0xDEADBEEF) received as expected");
        else
            $display("  FAIL  - got 0x%08H, 0x%08H, expected 0x%08H, 0xDEADBEEF", recv_buff[0], recv_buff[1], label_buff[0]);
        repeat (4) @(posedge aclk);
        end

        // =====================================================================
        // Summary
        // =====================================================================
        $display("");
        $display("=== Summary: %0d / 4 inference tests passed ===", total_pass);
        $display("=== All tests complete ===");
        $finish;
    end

    // -------------------------------------------------------------------------
    // Watchdog - abort if simulation runs away (500 µs wall time)
    // -------------------------------------------------------------------------
    initial begin
        #500_000_000;
        $display("WATCHDOG: simulation timeout!");
        $finish;
    end

endmodule

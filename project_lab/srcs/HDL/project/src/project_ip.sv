`timescale 1ns / 1ps

module project_ip
# (
    parameter integer N_W_HIDDEN  = 16, // 7 weights + 1 bias * 2 hidden neurons
    parameter integer N_W_OUTPUT  = 3, // 2 weights + 1 bias
    parameter integer N_X = 7,
    parameter integer  DATA_WIDTH = 32,
    parameter [DATA_WIDTH-1:0] INVALID_TOKEN = 32'hDEADBEEF, // for debugging

    localparam integer PTR_WIDTH = $clog2(N_W_HIDDEN),
    localparam integer ACC_DATA_WIDTH = 8,
    localparam integer WIDTH_W_HIDDEN = $clog2(N_W_HIDDEN/2),
    localparam integer WIDTH_W_OUTPUT = $clog2(N_W_OUTPUT),
    localparam integer WIDTH_X = $clog2(N_X)
)
(
	input wire                      aclk,
	input wire                      aresetn,
	input wire                      s_axis_tvalid,
	output reg                      s_axis_tready,
	input wire [DATA_WIDTH-1:0]     s_axis_tdata,
	input wire                      s_axis_tlast,
	output reg                      m_axis_tvalid,
	input wire                      m_axis_tready,
    output reg [DATA_WIDTH-1:0]     m_axis_tdata,
    output reg                      m_axis_tlast
);

    typedef enum logic [5:0] {
        IDLE          = 6'b000001,
        READ_W_HIDDEN = 6'b000010,
        READ_W_OUTPUT = 6'b000100,
        READ_X_ROW    = 6'b001000,
        ACCELERATE    = 6'b010000,
        LAST          = 6'b100000
    } state_t;



    reg [PTR_WIDTH-1:0] wptr;
    reg                 tlast_recvd;

    state_t state;

    // ============================================================
    //  RAM sizing:
    //    u_wh0 : 8 × 8-bit  → DEPTH_BITS = 3
    //    u_wh1 : 8 × 8-bit  → DEPTH_BITS = 3
    //    u_wo  : 3 × 8-bit  → DEPTH_BITS = 2 (4 locs, 1 wasted)
    //    u_xrow: 7 × 8-bit  → DEPTH_BITS = 3 (8 locs, 1 wasted)
    // ============================================================

    // ---- W_hidden col-0 ----
    reg                              wh0_write_en;
    wire                             wh0_read_en;
    reg  [WIDTH_W_HIDDEN-1:0]        wh0_waddr;
    wire [WIDTH_W_HIDDEN-1:0]        wh0_raddr;
    reg  [ACC_DATA_WIDTH-1:0]        wh0_din;
    wire [ACC_DATA_WIDTH-1:0]        wh0_dout;

    // ---- W_hidden col-1 ----
    reg                              wh1_write_en;
    wire                             wh1_read_en;
    reg  [WIDTH_W_HIDDEN-1:0]        wh1_waddr;
    wire [WIDTH_W_HIDDEN-1:0]        wh1_raddr;
    reg  [ACC_DATA_WIDTH-1:0]        wh1_din;
    wire [ACC_DATA_WIDTH-1:0]        wh1_dout;

    // ---- W_output ----
    reg                              wo_write_en;
    wire                             wo_read_en;
    reg  [WIDTH_W_OUTPUT-1:0]        wo_waddr;
    wire [WIDTH_W_OUTPUT-1:0]        wo_raddr;
    reg  [ACC_DATA_WIDTH-1:0]        wo_din;
    wire [ACC_DATA_WIDTH-1:0]        wo_dout;

    // ---- x_row ----
    reg                              xr_write_en;
    wire                             xr_read_en;
    reg  [WIDTH_X-1:0]               xr_waddr;
    wire [WIDTH_X-1:0]               xr_raddr;
    reg  [ACC_DATA_WIDTH-1:0]        xr_din;
    wire [ACC_DATA_WIDTH-1:0]        xr_dout;

    // ---- Accelerator handshake ----
    reg  acc_start;
    wire acc_done;
    wire [ACC_DATA_WIDTH-1:0] acc_result;

    // ---- AXIS helpers ----
    wire s_axis_en = s_axis_tvalid & s_axis_tready;
    wire m_axis_en = m_axis_tready & m_axis_tvalid;

    // ---- helpers ---- //
    wire [PTR_WIDTH-1:0] wptr_div2 = {1'b0, wptr[PTR_WIDTH-1:1]}; // divide by 2 for column addressing

    always_ff @(posedge aclk)
    begin
        if (~aresetn)
        begin
            s_axis_tready <= 1'b0;
            m_axis_tvalid <= 1'b0;
            m_axis_tdata  <= '0;
            m_axis_tlast  <= 1'b0;

            wh0_write_en <= '0; wh0_waddr <= '0; wh0_din <= '0;
            wh1_write_en <= '0; wh1_waddr <= '0; wh1_din <= '0;
            wo_write_en  <= '0; wo_waddr  <= '0; wo_din  <= '0;
            xr_write_en  <= '0; xr_waddr  <= '0; xr_din  <= '0;

            acc_start   <= 1'b0;

            wptr        <= '0;
            tlast_recvd <= 1'b0;

            state <= IDLE;
        end
        else
        begin
           // Defaults
            s_axis_tready <= 1'b0;
            m_axis_tvalid <= 1'b0;
            m_axis_tdata  <= '0;
            m_axis_tlast  <= 1'b0;

            wh0_write_en <= '0; wh0_waddr <= '0; wh0_din <= '0;
            wh1_write_en <= '0; wh1_waddr <= '0; wh1_din <= '0;
            wo_write_en  <= '0; wo_waddr  <= '0; wo_din  <= '0;
            xr_write_en  <= '0; xr_waddr  <= '0; xr_din  <= '0;

            acc_start <= 1'b0;

            case (state)
                IDLE:
                begin
                    s_axis_tready <= 1'b1;

                    wptr        <= '0;
                    tlast_recvd <= 1'b0;

                    state <= READ_W_HIDDEN;
                end

                READ_W_HIDDEN:
                begin
                    s_axis_tready   <= 1'b1;

                    if (s_axis_en)
                    begin

                        if (~wptr[0]) // col-0
                        begin
                            wh0_write_en <= 1'b1;
                            wh0_waddr    <= wptr_div2[WIDTH_W_HIDDEN-1:0];
                            wh0_din      <= s_axis_tdata[ACC_DATA_WIDTH-1:0];
                        end
                        else // col-1
                        begin
                            wh1_write_en <= 1'b1;
                            wh1_waddr    <= wptr_div2[WIDTH_W_HIDDEN-1:0];
                            wh1_din      <= s_axis_tdata[ACC_DATA_WIDTH-1:0];
                        end

                        if (s_axis_tlast)
                        begin
                            m_axis_tvalid <= 1'b1;
                            m_axis_tdata  <= INVALID_TOKEN;
                            m_axis_tlast  <= 1'b1;

                            wptr  <= '0;

                            state <= LAST;
                        end
                        else if ((wptr == (N_W_HIDDEN - 1)))
                        begin
                            wptr    <= '0;

                            state   <= READ_W_OUTPUT;
                        end
                        else
                        begin
                            wptr <= wptr + 1;
                        end
                    end
                end

                READ_W_OUTPUT:
                begin
                    s_axis_tready   <= 1'b1;

                    if (s_axis_en)
                    begin
                        wo_write_en  <= 1'b1;
                        wo_waddr     <= wptr[WIDTH_W_OUTPUT-1:0];
                        wo_din       <= s_axis_tdata[ACC_DATA_WIDTH-1:0];

                        if (s_axis_tlast)
                        begin
                            m_axis_tvalid <= 1'b1;
                            m_axis_tdata  <= INVALID_TOKEN;
                            m_axis_tlast  <= 1'b1;

                            wptr  <= '0;

                            state <= LAST;
                        end
                        else if ((wptr == (N_W_OUTPUT - 1)))
                        begin
                            wptr    <= '0;

                            state   <= READ_X_ROW;
                        end
                        else
                        begin
                            wptr <= wptr + 1;
                        end
                    end
                end

                READ_X_ROW:
                begin
                    s_axis_tready   <= 1'b1;

                    if (s_axis_en)
                    begin
                        xr_write_en  <= 1'b1;
                        xr_waddr     <= wptr[WIDTH_X-1:0];
                        xr_din       <= s_axis_tdata[ACC_DATA_WIDTH-1:0];

                        if (wptr == (N_X - 1))
                        begin
                            s_axis_tready   <= 1'b0;
                            acc_start <= 1'b1; // start accelerator after last feature is received

                            wptr    <= '0;
                            tlast_recvd <= s_axis_tlast; // capture tlast for later propagation

                            state   <= ACCELERATE;
                        end
                        else if (s_axis_tlast)             // tlast mid-row → genuinely invalid
                        begin
                            s_axis_tready   <= 1'b0;
                            m_axis_tvalid <= 1'b1;
                            m_axis_tdata  <= INVALID_TOKEN;
                            m_axis_tlast  <= 1'b1;

                            wptr  <= '0;

                            state <= LAST;
                        end
                        else
                        begin
                            wptr <= wptr + 1;
                        end
                    end
                end

                ACCELERATE:
                begin
                    if (acc_done)
                    begin
                        m_axis_tvalid <= 1'b1;
                        m_axis_tdata  <= {{(DATA_WIDTH-ACC_DATA_WIDTH){1'b0}}, acc_result};
                        m_axis_tlast  <= tlast_recvd; // propagate tlast to output

                        tlast_recvd <= 1'b0; // reset tlast flag for next round

                        state <= LAST;
                    end
                end

                LAST:
                begin
                    m_axis_tvalid <= 1'b1;
                    m_axis_tdata  <= m_axis_tdata;
                    m_axis_tlast  <= m_axis_tlast;

                    if (m_axis_en)
                    begin
                        m_axis_tvalid <= 1'b0;
                        m_axis_tdata  <= '0;
                        m_axis_tlast  <= '0;

                        if (m_axis_tlast)
                        begin
                            state <= IDLE;
                        end
                        else
                        begin
                            s_axis_tready   <= 1'b1;

                            wptr            <= '0;

                            state <= READ_X_ROW;
                        end
                    end
                end

                default:
                begin
                    state <= IDLE;
                end
            endcase
        end
    end

    accelerator #(
        .DATA_WIDTH (ACC_DATA_WIDTH),
        .N_W_HIDDEN (N_W_HIDDEN),
        .N_W_OUTPUT (N_W_OUTPUT),
        .N_X        (N_X)
    ) u_acc (
        .clk        (aclk),
        .rst        (~aresetn),
        .start      (acc_start),
        .done       (acc_done),
        .result     (acc_result),

        .wh0_read_en(wh0_read_en),
        .wh0_raddr  (wh0_raddr),
        .wh0_dout   (wh0_dout),

        .wh1_read_en(wh1_read_en),
        .wh1_raddr  (wh1_raddr),
        .wh1_dout   (wh1_dout),

        .wo_read_en (wo_read_en),
        .wo_raddr   (wo_raddr),
        .wo_dout    (wo_dout),

        .xr_read_en (xr_read_en),
        .xr_raddr   (xr_raddr),
        .xr_dout    (xr_dout)
    );

    memory_RAM #(
        .WIDTH      (ACC_DATA_WIDTH),
        .DEPTH_BITS (WIDTH_W_HIDDEN)   // 8 rows per column → 3
    ) u_wh0 (
        .clk           (aclk),
        .write_en      (wh0_write_en),
        .read_en       (wh0_read_en),
        .write_address (wh0_waddr),
        .read_address  (wh0_raddr),
        .data_in       (wh0_din),
        .data_out      (wh0_dout)
    );

    memory_RAM #(
        .WIDTH      (ACC_DATA_WIDTH),
        .DEPTH_BITS (WIDTH_W_HIDDEN)   // same as u_wh0
    ) u_wh1 (
        .clk           (aclk),
        .write_en      (wh1_write_en),
        .read_en       (wh1_read_en),
        .write_address (wh1_waddr),
        .read_address  (wh1_raddr),
        .data_in       (wh1_din),
        .data_out      (wh1_dout)
    );

    memory_RAM #(
        .WIDTH      (ACC_DATA_WIDTH),
        .DEPTH_BITS (WIDTH_W_OUTPUT)        // 3 locations → 2
    ) u_wo (
        .clk           (aclk),
        .write_en      (wo_write_en),
        .read_en       (wo_read_en),
        .write_address (wo_waddr),
        .read_address  (wo_raddr),
        .data_in       (wo_din),
        .data_out      (wo_dout)
    );

    memory_RAM #(
        .WIDTH      (ACC_DATA_WIDTH),
        .DEPTH_BITS (WIDTH_X)               // 7 features → 3
    ) u_xrow (
        .clk           (aclk),
        .write_en      (xr_write_en),
        .read_en       (xr_read_en),
        .write_address (xr_waddr),
        .read_address  (xr_raddr),
        .data_in       (xr_din),
        .data_out      (xr_dout)
    );

endmodule
`timescale 1ns / 1ps

module accelerator
#(
    parameter integer DATA_WIDTH = 8,   // matches project_ip
    parameter integer N_W_HIDDEN = 16,  // matches project_ip (total, both cols)
    parameter integer N_W_OUTPUT = 3,   // matches project_ip
    parameter integer N_X        = 7,   // matches project_ip

    // Derived address widths — do not override
    localparam integer N_WH_PER_COL         = N_W_HIDDEN / 2,
    localparam integer WH_ADDR_W            = $clog2(N_W_HIDDEN/2),
    localparam integer WO_ADDR_W            = $clog2(N_W_OUTPUT),
    localparam integer XR_ADDR_W            = $clog2(N_X),

    localparam integer MAC_N                = N_WH_PER_COL, // (8 rows per col)
    localparam integer MAC_FIXED_PT         = 8,
    localparam integer MAC_OUT_WIDTH        = ((2*DATA_WIDTH) - MAC_FIXED_PT) + $clog2(MAC_N)
)
(
    input  wire                    clk,
    input  wire                    rst,

    // Handshake
    input  wire                    start,
    output reg                     done,
    output reg  [DATA_WIDTH-1:0]   result,

    // ---- W_hidden col-0 read port ----
    output wire                    wh0_read_en,
    output reg  [WH_ADDR_W-1:0]    wh0_raddr,
    input  wire [DATA_WIDTH-1:0]   wh0_dout,

    // ---- W_hidden col-1 read port ----
    output wire                    wh1_read_en,
    output reg  [WH_ADDR_W-1:0]    wh1_raddr,
    input  wire [DATA_WIDTH-1:0]   wh1_dout,

    // ---- W_output read port ----
    output wire                    wo_read_en,
    output reg  [WO_ADDR_W-1:0]    wo_raddr,
    input  wire [DATA_WIDTH-1:0]   wo_dout,

    // ---- x_row read port ----
    output wire                    xr_read_en,
    output reg  [XR_ADDR_W-1:0]    xr_raddr,
    input  wire [DATA_WIDTH-1:0]   xr_dout
);
    // ---- Pipeline stage start cycles ----
    localparam integer C_WH0_READ_START     = 0;
    localparam integer C_WH1_READ_START     = 1;
    localparam integer C_XR_READ_START      = 1;
    localparam integer C_XR_EN_START        = C_XR_READ_START + 1;
    localparam integer C_WH0_EN_START       = C_WH0_READ_START + 1;
    localparam integer C_WH1_EN_START       = C_WH1_READ_START + 1;
    localparam integer C_MAC0_START         = C_WH0_READ_START + 2;
    localparam integer C_MAC1_START         = C_WH1_READ_START + 2;
    localparam integer C_XR_READ_DONE       = C_XR_READ_START + N_X;
    localparam integer C_WH0_READ_DONE      = C_WH0_READ_START + N_WH_PER_COL;
    localparam integer C_WH1_READ_DONE      = C_WH1_READ_START + N_WH_PER_COL;
    localparam integer C_XR_EN_DONE         = C_XR_EN_START + N_X;
    localparam integer C_WH0_EN_DONE        = C_WH0_EN_START + N_WH_PER_COL;
    localparam integer C_WH1_EN_DONE        = C_WH1_EN_START + N_WH_PER_COL;
    localparam integer C_MAC0_DONE          = C_MAC0_START + N_WH_PER_COL;
    localparam integer C_MAC1_DONE          = C_MAC1_START + N_WH_PER_COL;
    localparam integer C_MAC0_EN_DONE       = C_MAC0_DONE + 1;
    localparam integer C_MAC1_EN_DONE       = C_MAC1_DONE + 1;
    localparam integer C_SIG0_START         = C_MAC0_EN_DONE;
    localparam integer C_SIG1_START         = C_MAC1_EN_DONE;
    localparam integer C_WO_READ_START      = C_MAC0_DONE - 1;
    localparam integer C_WO_EN_START        = C_WO_READ_START + 1;
    localparam integer C_MAC0_START_R2      = C_MAC0_EN_DONE;
    localparam integer C_WO_READ_DONE       = C_WO_READ_START + N_W_OUTPUT;
    localparam integer C_WO_EN_DONE         = C_WO_EN_START + N_W_OUTPUT;
    localparam integer C_MAC0_DONE_R2       = C_MAC0_START_R2 + N_W_OUTPUT;
    localparam integer C_MAC0_EN_DONE_R2    = C_MAC0_DONE_R2 + 1;
    localparam integer C_SIG_RES            = C_MAC0_EN_DONE_R2;
    localparam integer C_SIG_RES_DONE       = C_SIG_RES + 1;
    localparam integer C_RES_THRESHOLD      = C_SIG_RES_DONE + 1;
    localparam integer ACC_LATENCY          = C_RES_THRESHOLD + 1; // total

    // ... output MAC stages follow from C_H1_CAPTURE
    typedef enum logic [1:0] {
        IDLE            = 2'b01,
        ACCELERATING    = 2'b10
    } state_t;

    reg [31:0]          cntr;

    reg [DATA_WIDTH-1:0]  xr_dout_dly;  // for MAC input setup

    state_t state;

    // ---- MAC wires ----
    wire                      mac0_en,      mac1_en;
    wire                      mac0_clr,     mac1_clr;
    logic                     mac0_dsbl_fp, mac1_dsbl_fp;
    logic  [DATA_WIDTH-1:0]   mac0_a,       mac0_b;
    logic  [DATA_WIDTH-1:0]   mac1_a,       mac1_b;
    wire [MAC_OUT_WIDTH-1:0]  mac0_out,     mac1_out;

    // ---- Sigmoid LUT wires ----
    logic [DATA_WIDTH-1:0] sigmoid_in;
    wire  [DATA_WIDTH-1:0] sigmoid_out; // valid 1 cycle after sigmoid_in is set (registered LUT)

    // --- //

    assign wh0_read_en = ((cntr >= C_WH0_EN_START) & (cntr < C_WH0_EN_DONE));

    always_ff @(posedge clk) begin
        if (rst)
            wh0_raddr <= '0;
        else
        begin
            wh0_raddr <= '0;

            if ((cntr >= C_WH0_READ_START) & (cntr < C_WH0_READ_DONE))
                wh0_raddr <= WH_ADDR_W'(cntr - C_WH0_READ_START);
        end
    end

    // --- //

    assign wh1_read_en = ((cntr >= C_WH1_EN_START) & (cntr < C_WH1_EN_DONE));

    always_ff @(posedge clk) begin
        if (rst)
            wh1_raddr <= '0;
        else
        begin
            wh1_raddr <= '0;

            if ((cntr >= C_WH1_READ_START) & (cntr < C_WH1_READ_DONE))
                wh1_raddr <= WH_ADDR_W'(cntr - C_WH1_READ_START);
        end
    end

    // --- //
    assign wo_read_en = ((cntr >= C_WO_EN_START) & (cntr < C_WO_EN_DONE));

    always_ff @(posedge clk) begin
        if (rst)
            wo_raddr <= '0;
        else
        begin
            wo_raddr <= '0;

            if ((cntr >= C_WO_READ_START) & (cntr < C_WO_READ_DONE))
                wo_raddr <= WO_ADDR_W'(cntr - C_WO_READ_START);
        end
    end

    // --- //

    assign xr_read_en = ((cntr >= C_XR_EN_START) & (cntr < C_XR_EN_DONE));

    always_ff @(posedge clk) begin
        if (rst)
        begin
            xr_raddr <= '0;

            xr_dout_dly <= '0;
        end
        else
        begin
            xr_raddr <= '0;

            xr_dout_dly <= xr_dout;
            if ((cntr >= C_XR_READ_START) & (cntr < C_XR_READ_DONE))
                xr_raddr <= XR_ADDR_W'(cntr - C_XR_READ_START);
        end
    end

    // --- //

    assign mac0_en  = ((cntr >= C_MAC0_START) & (cntr < C_MAC0_EN_DONE)) | ((cntr >= C_MAC0_START_R2) & (cntr < C_MAC0_EN_DONE_R2));
    assign mac0_clr = ((cntr == C_MAC0_DONE) | (cntr == C_MAC0_DONE_R2));

    always_comb
    begin
        mac0_dsbl_fp = 1'b0; // default use fixed point for scaling
        mac0_a = '0;
        mac0_b = '0;

        if ((cntr >= C_MAC0_START) & (cntr < C_MAC0_DONE))
        begin
            if (cntr == C_MAC0_START)
            begin
                mac0_dsbl_fp    = 1'b1; // disable fixed point for bias
                mac0_a          = DATA_WIDTH'(1);
            end
            else
                mac0_a = xr_dout;

            mac0_b = wh0_dout;
        end

        else if ((cntr >= C_MAC0_START_R2) & (cntr < C_MAC0_DONE_R2))
        begin
            if (cntr == C_MAC0_START_R2)
            begin
                mac0_dsbl_fp    = 1'b1; // disable fixed point for bias
                mac0_a          = DATA_WIDTH'(1);
            end
            else
                mac0_a = sigmoid_out;

            mac0_b = wo_dout;
        end

    end

    // --- //

    assign mac1_en  = ((cntr >= C_MAC1_START) & (cntr < C_MAC1_EN_DONE));
    assign mac1_clr = (cntr == C_MAC1_DONE);

    always_comb
    begin
        mac1_dsbl_fp = 1'b0; // default use fixed point for scaling
        mac1_a = '0;
        mac1_b = '0;

        if ((cntr >= C_MAC1_START) & (cntr < C_MAC1_DONE))
        begin
            if (cntr == C_MAC1_START)
            begin
                mac1_dsbl_fp    = 1'b1; // disable fixed point for bias
                mac1_a          = DATA_WIDTH'(1);
            end
            else
                mac1_a = xr_dout_dly;

            mac1_b = wh1_dout;
        end

    end

    // --- //
    always_comb
    begin
        sigmoid_in = '0;

        if ((cntr == C_SIG0_START) | (cntr == C_SIG_RES))
            sigmoid_in = mac0_out[DATA_WIDTH-1:0];

        else if (cntr == C_SIG1_START)
            sigmoid_in = mac1_out[DATA_WIDTH-1:0];
    end


    // --- //
    always_ff @(posedge clk) begin
        if (rst)
        begin
            result <= '0;
        end
        else
        begin
            case (cntr)
                C_SIG_RES_DONE:
                    result <= sigmoid_out;

                C_RES_THRESHOLD:
                    result <= {{(DATA_WIDTH-1){1'b0}}, (result > DATA_WIDTH'(128))}; // clamp to DATA_WIDTH bits

                default :
                    result <= '0;
            endcase
        end
    end


    always_ff @(posedge clk) begin
        if (rst)
        begin
            done <= 1'b0;

            cntr <= '0;

            state <= IDLE;
        end
        else
        begin
            done <= 1'b0;

            case (state)
                IDLE:
                begin
                    cntr <= '0;

                    if (start)
                    begin
                        state <= ACCELERATING;
                    end
                end

                ACCELERATING:
                begin
                    if (cntr == (ACC_LATENCY - 1))
                    begin
                        done <= 1'b1;

                        cntr <= '0;

                        state <= IDLE;
                    end
                    else
                    begin
                        cntr <= cntr + 1;
                    end
                end

                default:
                begin
                    state <= IDLE;
                end
            endcase
        end
    end

    // ---- MAC 0 — hidden neuron 0 (wh0 column) ----
    mac #(
        .DATA_WIDTH  (DATA_WIDTH),
        .N           (MAC_N),
        .FIXED_POINT (MAC_FIXED_PT)
    ) u_mac0 (
        .clk (clk),
        .rst (rst),
        .en  (mac0_en),
        .clr (mac0_clr),
        .dsbl_fp (mac0_dsbl_fp),
        .a   (mac0_a),      // x[i]
        .b   (mac0_b),      // wh0[i]
        .out (mac0_out)
    );

    // ---- MAC 1 — hidden neuron 1 (wh1 column) ----
    mac #(
        .DATA_WIDTH  (DATA_WIDTH),
        .N           (MAC_N),
        .FIXED_POINT (MAC_FIXED_PT)
    ) u_mac1 (
        .clk (clk),
        .rst (rst),
        .en  (mac1_en),
        .clr (mac1_clr),
        .dsbl_fp (mac1_dsbl_fp),
        .a   (mac1_a),      // x[i]  (same x, different weight)
        .b   (mac1_b),      // wh1[i]
        .out (mac1_out)
    );

    // ---- Sigmoid ----
    int8_sigmoid_lut u_sigmoid (
    .clk           (clk),
    .rst           (rst),
    .value         (sigmoid_in),
    .sigmoid_value (sigmoid_out)
);

endmodule
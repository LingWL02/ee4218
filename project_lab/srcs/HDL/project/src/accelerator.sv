`timescale 1ns / 1ps

module accelerator
#(
    parameter integer N_W_HIDDEN = 16,  // matches project_ip (total, both cols)
    parameter integer N_W_OUTPUT = 3,   // matches project_ip
    parameter integer N_X        = 7,   // matches project_ip

    // Derived address widths — do not override
    localparam integer WH_ADDR_W = $clog2(N_W_HIDDEN/2),
    localparam integer WO_ADDR_W = $clog2(N_W_OUTPUT),
    localparam integer XR_ADDR_W = $clog2(N_X)
    localparam integer ACC_LATENCY = 32 // TODO: replace with actual accelerator latency
)
(
    input  wire                    clk,
    input  wire                    rst,

    // Handshake
    input  wire                    start,
    output reg                     done,
    output reg  [7:0]              result,

    // ---- W_hidden col-0 read port ----
    output reg                     wh0_read_en,
    output reg  [WH_ADDR_W-1:0]    wh0_raddr,
    input  wire [7:0]              wh0_dout,

    // ---- W_hidden col-1 read port ----
    output reg                     wh1_read_en,
    output reg  [WH_ADDR_W-1:0]    wh1_raddr,
    input  wire [7:0]              wh1_dout,

    // ---- W_output read port ----
    output reg                     wo_read_en,
    output reg  [WO_ADDR_W-1:0]    wo_raddr,
    input  wire [7:0]              wo_dout,

    // ---- x_row read port ----
    output reg                     xr_read_en,
    output reg  [XR_ADDR_W-1:0]    xr_raddr,
    input  wire [7:0]              xr_dout
);
    typedef enum logic [1:0] {
        IDLE            = 2'b01,
        ACCELERATING    = 2'b10,
    } state_t;

    reg [31:0] cntr;
    state_t state;

    always_ff @(posedge clk) begin
        if (rst)
        begin
            done <= 1'b0;
            result <= '0;

            cntr <= '0;

            state <= IDLE;
        end
        else
        begin
            done <= 1'b0;
            result <= '0;

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
                    if (cntr == ACC_LATENCY - 1)
                    begin
                        done <= 1'b1;
                        result <= 8'hFF; // TODO: replace with actual accelerator output

                        cntr <= '0;
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

    // TODO: internal logic

endmodule
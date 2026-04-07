`timescale 1ns / 1ps

module mac
#(
    parameter  integer DATA_WIDTH = 8,
    parameter  integer N = 8,
    parameter  integer FIXED_POINT = 8,
    localparam integer CLOG2N = $clog2(N),
    localparam integer AXB_WIDTH = 2*DATA_WIDTH,
    localparam integer ACC_WIDTH = AXB_WIDTH+CLOG2N,
    localparam integer OUT_WIDTH = ACC_WIDTH - FIXED_POINT
)
(
    input wire                      clk,
    input wire                      rst,
    input wire                      en,
    input wire                      clr,
    input wire                      dsbl_fp,
    input wire  [DATA_WIDTH-1:0]    a,
    input wire  [DATA_WIDTH-1:0]    b,
    output wire [OUT_WIDTH-1:0]     out
);

reg                         clr_dly;
wire    [AXB_WIDTH-1:0]     axb = a * b;
reg     [AXB_WIDTH-1:0]     axb_reg;
reg     [ACC_WIDTH-1:0]     acc;

assign out = acc[ACC_WIDTH-1:FIXED_POINT];

always_ff @(posedge clk) begin
    if (rst)
    begin
        clr_dly <= '0;
        axb_reg <= '0;
    end
    begin
        clr_dly <= clr;

        if (clr)
            axb_reg <= '0;
        else if (en)
        begin
            if (dsbl_fp)
                axb_reg <= {axb[AXB_WIDTH-FIXED_POINT-1:0], {FIXED_POINT{1'b0}}};
            else
                axb_reg <= axb;
        end
    end
end

always_ff @(posedge clk) begin
    if (rst | clr_dly)
    begin
        acc <= '0;
    end
    else if (en)
        acc <= acc + {{CLOG2N{1'b0}}, axb_reg};
end

endmodule
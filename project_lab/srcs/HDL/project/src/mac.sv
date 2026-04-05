`timescale 1ns / 1ps

module mac
#(
    parameter WIDTH = 8,
    parameter N = 2,  // CLOG2(N iterations)
    parameter FIXED_POINT = 8,
    localparam AXB_WIDTH = 2*WIDTH,
    localparam AXBFP_WIDTH = AXB_WIDTH-FIXED_POINT,
    localparam OUT_WIDTH = AXBFP_WIDTH+N
)
(
    input clk,
    input rst,
    input en,
    input clr,
    input [WIDTH-1:0] a,
    input [WIDTH-1:0] b,
    output reg [OUT_WIDTH-1:0] out,
);

wire    [AXB_WIDTH-1:0]     axb = a * b;
reg     [AXBFP_WIDTH-1:0]   axbfp_mul;

always_ff @(posedge clk) begin
    if (rst | clr)
    begin
        axbfp_mul <= 1'b0;
    end
    else
    begin
        if (en)
        begin
            axbfp_mul   <= axb[(AXB_WIDTH-1)-:AXBFP_WIDTH];
        end
    end
end

always_ff @(posedge clk) begin
    if (rst | clr)
    begin
        out <= 1'b0;
    end
    else
    begin
        if (en)
        begin
            out <= out + {{N{1'b0}}, axbfp_mul};
        end
    end
end

endmodule
module mac
#(
    parameter width = 8,
    parameter n = 2,
    parameter fixed_point = 8,
    localparam axb_width = 2*width,
    localparam axbfp_width = axb_width-fixed_point;
    localparam out_width = axbfp_width+n,
)
(
    input clk,
    input aresetn,
    input en,
    input clear,
    input [width-1:0] a,
    input [width-1:0] b,
    output reg [out_width-1:0] out,
    output reg done
)

wire    [axb_width-1:0]     axb = a * b;
reg     [axbfp_width-1:0]   axbfp_mul;
reg                         en_mul;
reg                         clear_mul;

always_ff @(posedge clk) begin
    if (~aresetn)
    begin
        axbfp_mul   <= 1'b0;
        en_mul      <= 1'b0;
        clear_mul   <= 1'b0;
    end
    else
    begin
        clear_mul       <= clear;
        if (clear)
        begin
            axbfp_mul   <= 1'b0;
            en_mul      <= 1'b0;
        end
        else
        begin
            en_mul              <= en;
            if (en) axbfp_mul   <= axb[(axb_width-1)-:axbfp_width];
        end
    end
end

always_ff @(posedge clk) begin
    if (~aresetn)
    begin
        out     <= 1'b0;
        done    <= 1'b0;
    end
    else
    begin
        done                <= 1'b0;
        if (clear_mul) out  <= 1'b0;
        else
        begin
            done            <= en_mul & ~en;
            if (en_mul) out <= out + {n{1'b0}, axbfp_mul};
        end
    end
end

endmodule
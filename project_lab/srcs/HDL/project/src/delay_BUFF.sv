`timescale 1ns / 1ps

module delay_BUFF
#(
    parameter WIDTH = 1
)
(
    input  wire [WIDTH-1:0] I,
    output wire [WIDTH-1:0] O
);
    genvar i;
    generate
        for (i = 0; i < WIDTH; i = i + 1)
        begin : inv_buf_gen
            wire inv;
            (* DONT_TOUCH = "yes" *) INV inv_inst (.I(I[i]), .O(inv));
            (* DONT_TOUCH = "yes" *) INV inv2_inst (.I(inv), .O(O[i]));
        end
    endgenerate
endmodule

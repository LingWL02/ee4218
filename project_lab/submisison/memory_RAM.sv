`timescale 1ns / 1ps

module memory_RAM
#(
	parameter integer WIDTH 		= 8, 	// width is the number of bits per location
	parameter integer DEPTH_BITS	= 2		// depth is the number of locations (2^number of address bits)
)
(
	input wire					clk,
	input wire					write_en,
	input wire                 	read_en,
	input wire [DEPTH_BITS-1:0] write_address,
	input wire [DEPTH_BITS-1:0] read_address,
	input wire [WIDTH-1:0] 		data_in,
	output reg [WIDTH-1:0]		data_out
);

    reg [WIDTH-1:0] RAM [0:2**DEPTH_BITS-1];
    wire en = write_en | read_en;

	always @(posedge clk)
	begin
		 if (en)
		 begin
			if (write_en)
			begin
				RAM[write_address] <= data_in;
			end
			else
			begin
				data_out <= RAM[read_address];
			end
		end
	end

endmodule

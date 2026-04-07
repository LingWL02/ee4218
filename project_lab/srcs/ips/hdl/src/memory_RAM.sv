`timescale 1ns / 1ps

module memory_RAM
#(
	parameter integer WIDTH 		= 8, 					// width is the number of bits per location
	parameter integer DEPTH_BITS	= 2				// depth is the number of locations (2^number of address bits)
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

	// wire [DEPTH_BITS-1:0] 	buff_wr_addr;
	// wire [DEPTH_BITS-1:0] 	buff_rd_addr;
	// wire [WIDTH-1:0] 		buff_data_in;

	// reg [WIDTH-1:0] 		data_out_buff;

	// delay_BUFF #(.WIDTH(DEPTH_BITS)) del_buff_wr_addr (
	// 	.I(write_address),
	// 	.O(buff_wr_addr)
	// );

	// delay_BUFF #(.WIDTH(DEPTH_BITS)) del_buff_rd_addr (
	// 	.I(read_address),
	// 	.O(buff_rd_addr)
	// );

	// delay_BUFF #(.WIDTH(WIDTH)) del_buff_data_in (
	// 	.I(data_in),
	// 	.O(buff_data_in)
	// );

	// delay_BUFF #(.WIDTH(WIDTH)) del_buff_data_out (
	// 	.I(data_out_buff),
	// 	.O(data_out)
	// );


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

`timescale 1ns / 1ps

/*
----------------------------------------------------------------------------------
--	(c) Rajesh C Panicker, NUS
--  Description : Module implementing a single port fully synchronous RAM to act as local memory for the AXI Stream Coprocessor
--	License terms :
--	You are free to use this code as long as you
--		(i) DO NOT post a modified version of this on any public repository;
--		(ii) use it only for educational purposes;
--		(iii) accept the responsibility to ensure that your implementation does not violate any intellectual property of any entity.
--		(iv) accept that the program is provided "as is" without warranty of any kind or assurance regarding its suitability for any particular purpose;
--		(v) send an email to rajesh.panicker@ieee.org briefly mentioning its use (except when used for the course EE4218 at the National University of Singapore);
--		(vi) retain this notice in this file or any files derived from this.
----------------------------------------------------------------------------------
*/

// width is the number of bits per location; depth_bits is the number of address bits. 2^depth_bits is the number of locations

module memory_RAM
#(
	parameter integer WIDTH 		= 8, 					// width is the number of bits per location
	parameter integer DEPTH_BITS 	= 2				// depth is the number of locations (2^number of address bits)
)
(
	input 					clk,
	input 					en,
	input 					wr,
	input [DEPTH_BITS-1:0] 	address,
	input [WIDTH-1:0] 		data_in,
	output reg [WIDTH-1:0]	data_out
);

    reg [WIDTH-1:0] RAM [0:2**DEPTH_BITS-1];
    wire [DEPTH_BITS-1:0] address;
    wire enable;

	always @(posedge clk)
	begin
		 if (en)
		 begin
			if (wr)
				RAM[address] <= data_in;
			else
				data_out <= RAM[address];
		end
	end

endmodule

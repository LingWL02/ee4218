`timescale 1ns / 1ps

/*
----------------------------------------------------------------------------------
--	(c) Rajesh C Panicker, NUS
--  Description : Template for the Matrix Multiply unit for the AXI Stream Coprocessor
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

// those outputs which are assigned in an always block of matrix_multiply shoud be changes to reg (such as output reg Done).

module matrix_multiply
#(
	parameter width = 8, 			// width is the number of bits per location
	parameter A_depth_bits = 3, 	// depth is the number of locations (2^number of address bits)
	parameter B_depth_bits = 2,
	parameter RES_depth_bits = 1
)
(
	input                           clk,                        // Clock signal
	input                           aresetn,                    // Active low reset
	input                           Start,                      // myip_v1_0 -> matrix_multiply_0
	output reg                      Done,                       // matrix_multiply_0 -> myip_v1_0

	output reg                      A_read_en,                  // matrix_multiply_0 -> A_RAM
	output reg [A_depth_bits-1:0]   A_read_address,             // matrgix_multiply_0 -> A_RAM
	input      [width-1:0]          A_read_data_out,            // A_RAM -> matrix_multiply_0

	output reg                      B_read_en,                  // matrix_multiply_0 -> B_RAM
	output reg [B_depth_bits-1:0]   B_read_address,             // matrix_multiply_0 -> B_RAM
	input      [width-1:0]          B_read_data_out,            // B_RAM -> matrix_multiply_0

	output reg                      RES_write_en,               // matrix_multiply_0 -> RES_RAM
	output reg [RES_depth_bits-1:0] RES_write_address,          // matrix_multiply_0 -> RES_RAM
	output reg [width-1:0]          RES_write_data_in           // matrix_multiply_0 -> RES_RAM
);

	localparam N_WORDS_A 	= 2**A_depth_bits;
	localparam N_WORDS_B 	= 2**B_depth_bits;
	localparam N_WORDS_RES 	= 2**RES_depth_bits;

	localparam mac_out_width 	= width + B_depth_bits;
	localparam iter_width 		= $clog2(N_WORDS_RES);

	// implement the logic to read A_RAM, read B_RAM, do the multiplication and write the results to RES_RAM
	// Note: A_RAM and B_RAM are to be read synchronously. Read the wiki for more details.

	reg 	[1:0]			state;
	reg 	[iter_width:0] 	iter;
	reg	 					iter_end;

	reg 					A_read_en_dly;
	reg 					B_read_en_dly;

	// MAC module wires
	reg mac_en;
	reg mac_clear;
	reg [width-1:0] mac_a;
	reg [width-1:0] mac_b;
	wire [mac_out_width-1:0] mac_out;
	wire mac_done;

	// One-hot encoded states
	localparam IDLE      = 2'b01;
	localparam COMPUTE   = 2'b10;

	always_ff @(posedge clk) begin
		if (~aresetn)
		begin
			mac_en 		<= 1'b0;
			mac_clear 	<= 1'b1;
			mac_a 		<= {width{1'b0}};
			mac_b 		<= {width{1'b0}};

			A_read_en_dly <= 1'b0;
			B_read_en_dly <= 1'b0;
		end
		else
		begin
			mac_en 		<= 1'b0;
			mac_clear 	<= 1'b1;

			A_read_en_dly <= A_read_en;
			B_read_en_dly <= B_read_en;

			if (A_read_en_dly & B_read_en_dly)
			begin
				mac_en 		<= 1'b1;
				mac_clear 	<= 1'b0;
				mac_a 		<= A_read_data_out;
				mac_b 		<= B_read_data_out;
			end
		end
	end

	always_ff @(posedge clk) begin
		if (~aresetn)
		begin
			Done 				<= 1'b0;
			A_read_en 			<= 1'b0;
			A_read_address 		<= {A_depth_bits{1'b0}};
			B_read_en 			<= 1'b0;
			B_read_address 		<= {B_depth_bits{1'b0}};
			RES_write_en 		<= 1'b0;
			RES_write_address 	<= {RES_depth_bits{1'b0}};
			RES_write_data_in 	<= {width{1'b0}};

			state 	<= IDLE;
			iter 	<= 1'b0;
		end
		else
		begin
			Done 				<= 1'b0;
			A_read_en 			<= 1'b0;
			A_read_address 		<= {A_depth_bits{1'b0}};
			B_read_en 			<= 1'b0;
			B_read_address 		<= {B_depth_bits{1'b0}};
			RES_write_en 		<= 1'b0;
			RES_write_address 	<= {RES_depth_bits{1'b0}};
			RES_write_data_in 	<= {width{1'b0}};

			case (state)
				IDLE:
				begin
					iter 		<= {iter_width{1'b0}};
					iter_end 	<= 1'b0;

					if (Start)
					begin
						state 			<= COMPUTE;
						A_read_en 		<= 1'b1;
						A_read_address 	<= {A_depth_bits{1'b0}};

						B_read_en 		<= 1'b1;
						B_read_address 	<= {B_depth_bits{1'b0}};
					end
				end

				COMPUTE:
				begin
					// Latch current addresses, act as counters
					A_read_address <= A_read_address;
					B_read_address <= B_read_address;

					if (iter_end & mac_done)
					begin
						RES_write_en 		<= 1'b1;
						RES_write_address 	<= iter;
						RES_write_data_in 	<= mac_out[width-1:0];

						if (iter == (N_WORDS_RES - 1))
						begin
							state 	<= IDLE;
							Done 	<= 1'b1;
						end
						else
						begin
							iter 		<= iter + 1'b1;
							iter_end 	<= 1'b0;

							A_read_en 		<= 1'b1;
							A_read_address 	<= A_read_address + 1'b1;

							B_read_en 		<= 1'b1;
							B_read_address 	<= 1'b0;
						end
					end
					else if (~iter_end)
					begin
						if (B_read_address == (N_WORDS_B - 1)) iter_end <= 1'b1;
						else
						begin
							A_read_en 		<= 1'b1;
							A_read_address 	<= A_read_address + 1'b1;

							B_read_en 		<= 1'b1;
							B_read_address 	<= B_read_address + 1'b1;
						end
					end
				end
				default: state <= IDLE;
			endcase
		end
	end

	// MAC module instantiation
	mac
	#(
		.width(width),
		.n(B_depth_bits),
		.fixed_point(width)
	)
	mac_donalds
	(
		.clk(clk),
		.aresetn(aresetn),
		.en(mac_en),
		.clear(mac_clear),
		.a(mac_a),
		.b(mac_b),
		.out(mac_out),
		.done(mac_done)
	);
endmodule

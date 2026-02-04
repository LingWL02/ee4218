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
	#(	parameter width = 8, 			// width is the number of bits per location
		parameter A_depth_bits = 3, 	// depth is the number of locations (2^number of address bits)
		parameter B_depth_bits = 2, 
		parameter RES_depth_bits = 1
	) 
	(
		input clk,										
		input Start,									// myip_v1_0 -> matrix_multiply_0.
		output reg Done,									// matrix_multiply_0 -> myip_v1_0. Possibly reg.
		
		output reg A_read_en,  								// matrix_multiply_0 -> A_RAM. Possibly reg.
		output reg [A_depth_bits-1:0] A_read_address, 		// matrix_multiply_0 -> A_RAM. Possibly reg.
		input [width-1:0] A_read_data_out,				// A_RAM -> matrix_multiply_0.
		
		output reg B_read_en, 								// matrix_multiply_0 -> B_RAM. Possibly reg.
		output reg [B_depth_bits-1:0] B_read_address, 		// matrix_multiply_0 -> B_RAM. Possibly reg.
		input [width-1:0] B_read_data_out,				// B_RAM -> matrix_multiply_0.
		
		output reg RES_write_en, 							// matrix_multiply_0 -> RES_RAM. Possibly reg.
		output reg [RES_depth_bits-1:0] RES_write_address, 	// matrix_multiply_0 -> RES_RAM. Possibly reg.
		output reg [width-1:0] RES_write_data_in 			// matrix_multiply_0 -> RES_RAM. Possibly reg.
	);
	
	// implement the logic to read A_RAM, read B_RAM, do the multiplication and write the results to RES_RAM
	// Note: A_RAM and B_RAM are to be read synchronously. Read the wiki for more details.
    // Internal registers
	localparam Idle  = 4'b1000;
	localparam Run = 4'b0100;
	localparam Write = 4'b0010;
	// localparam Write_Outputs  = 4'b0001;

    reg [3:0] cycle_count;
    reg [width-1:0] a_val, b_val;
    reg [15:0] sum1, sum2;
    reg [1:0] res_index;
    reg [2:0] idx;
    reg [1:0] phase;
    reg [3:0] state = Idle;
    // Pipeline registers for previous cycle's data
    reg [width-1:0] a_val_prev, b_val_prev;
    reg [2:0] idx_prev;
    reg [1:0] res_index_prev;



        always @(posedge clk) begin
            case (state)
                Idle: begin
                    if (Start) begin
                        cycle_count <= 0;
                        A_read_en        <= 0;
                        B_read_en        <= 0;
                        A_read_address   <= 0;
                        B_read_address   <= 0;
                        a_val            <= 0;
                        b_val            <= 0;
                        sum1             <= 0;
                        sum2             <= 0;
                        RES_write_en     <= 0;
                        RES_write_address<= 0;
                        RES_write_data_in<= 0;
                        Done             <= 0;
                        res_index        <= 0;
                        idx              <= 0;
                        phase            <= 0;
                        if (Start) begin
                            state <= Run;
                        end
                    end
                end
                Run: begin

                    // Latch current RAM outputs for next multiply
                    a_val_prev     <= a_val;
                    b_val_prev     <= b_val;
                    idx_prev       <= idx;
                    res_index_prev <= res_index;

                    // Set up next read
                    A_read_en      <= 1;
                    B_read_en      <= 1;
                    A_read_address <= res_index * 4 + idx; // row-major order
                    B_read_address <= idx;

                    // Store current outputs for next cycle
                    if (cycle_count == 0) begin
                        a_val <= 0;
                        b_val <= 0;
                    end else begin
                        a_val <= A_read_data_out;
                        b_val <= B_read_data_out;
                    end

                    // Multiply and accumulate previous values (skip on first cycle)
                    if (cycle_count > 0) begin
                        if (res_index_prev == 0)
                            sum1 <= sum1 + (a_val_prev * b_val_prev);
                        else
                            sum2 <= sum2 + (a_val_prev * b_val_prev);
                    end

                    // Advance idx and check if row is done
                    if (idx == 3 && cycle_count > 0) begin
                        idx <= 0;
                        cycle_count <= 0;
                        state <= Write;
                    end else begin
                        idx <= idx + 1;
                        cycle_count <= cycle_count + 1;
                    end
                end

                Write: begin
                    // Accumulate the LAST product before writing
                    if (cycle_count == 0) begin
                        if (res_index == 0)
                            sum1 <= sum1 + (a_val_prev * b_val_prev);
                        else
                            sum2 <= sum2 + (a_val_prev * b_val_prev);
                        cycle_count <= 1;
                    end else begin
                        RES_write_en      <= 1;
                        RES_write_address <= res_index;
                        if (res_index == 0) begin
                            RES_write_data_in <= sum1[15:8];
                            sum1              <= 0;
                        end else begin
                            RES_write_data_in <= sum2[15:8];
                            sum2              <= 0;
                        end
                        
                        if (res_index == 1) begin
                            Done  <= 1;
                            state <= Idle;
                            RES_write_en <= 0;
                        end else begin
                            res_index <= res_index + 1;
                            cycle_count <= 0;
                            state <= Run;
                            RES_write_en <= 0;
                        end
                    end
                end
        endcase
    end

//     always @(posedge clk) begin
//         if (!Start) begin
//             cycle_count      <= 0;
//             A_read_en        <= 0;
//             B_read_en        <= 0;
//             A_read_address   <= 0;
//             B_read_address   <= 0;
//             a_val            <= 0;
//             b_val            <= 0;
//             sum1             <= 0;
//             sum2             <= 0;
//             RES_write_en     <= 0;
//             RES_write_address<= 0;
//             RES_write_data_in<= 0;
//             Done             <= 0;
//             res_index        <= 0;
//             idx              <= 0;
//             phase            <= 0;
//         end
//         else 
//         begin
//             case (phase)
//                 2'b00: begin // Read A
//                     A_read_en      <= 1;
//                     A_read_address <= cycle_count[2:0];
//                     a_val          <= A_read_data_out;
//                     phase          <= 2'b01;
//                 end
//                 2'b01: begin // Read B
//                     B_read_en      <= 1;
//                     B_read_address <= cycle_count[1:0];
//                     b_val          <= B_read_data_out;
//                     phase          <= 2'b10;
//                 end
//                 2'b10: begin // Multiply and Accumulate
//                     if (res_index == 0) begin
//                         sum1 <= sum1 + (a_val * b_val);
//                     end else begin
//                         sum2 <= sum2 + (a_val * b_val);
//                     end
//                     idx <= idx + 1;
//                     if (idx == 3) begin
//                         phase <= 2'b11; // Move to Write phase
//                     end else begin
//                         phase <= 2'b00; // Back to Read A
//                     end
//                 end
//                 2'b11: begin // Write Result
//                     RES_write_en      <= 1;
//                     RES_write_address <= res_index;
//                     if (res_index == 0) begin
//                         RES_write_data_in <= sum1[15:8];
//                         sum1              <= 0; // Reset sum1 for next use
//                     end else begin
//                         RES_write_data_in <= sum2[15:8];
//                         sum2              <= 0; // Reset sum2 for next use
//                     end
//                     res_index <= res_index + 1;
//                     if (res_index == 1) begin
//                         Done <= 1; // All results written
//                     end else begin
//                         phase <= 2'b00; // Back to Read A for next result
//                     end
//                 end
//             endcase
//             cycle_count <= cycle_count + 1;
//         end
//     end
endmodule



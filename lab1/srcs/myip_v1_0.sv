/*
----------------------------------------------------------------------------------
--	(c) Rajesh C Panicker, NUS
--  Description : Matrix Multiplication AXI Stream Coprocessor. Based on the orginal AXIS Coprocessor template (c) Xilinx Inc
-- 	Based on the orginal AXIS coprocessor template (c) Xilinx Inc
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
/*
-------------------------------------------------------------------------------
--
-- Definition of Ports
-- ACLK              : Synchronous clock
-- ARESETN           : System reset, active low
-- S_AXIS_TREADY  : Ready to accept data in
-- S_AXIS_TDATA   :  Data in
-- S_AXIS_TLAST   : Optional data in qualifier
-- S_AXIS_TVALID  : Data in is valid
-- M_AXIS_TVALID  :  Data out is valid
-- M_AXIS_TDATA   : Data Out
-- M_AXIS_TLAST   : Optional data out qualifier
-- M_AXIS_TREADY  : Connected slave device is ready to accept data out
--
-------------------------------------------------------------------------------
*/

module myip_v1_0
# (
	parameter m = 2,
	parameter n = 4,
	parameter width = 8
)
(
	// DO NOT EDIT BELOW THIS LINE ////////////////////
	ACLK,
	ARESETN,
	S_AXIS_TREADY,
	S_AXIS_TDATA,
	S_AXIS_TLAST,
	S_AXIS_TVALID,
	M_AXIS_TVALID,
	M_AXIS_TDATA,
	M_AXIS_TLAST,
	M_AXIS_TREADY
	// DO NOT EDIT ABOVE THIS LINE ////////////////////
);

	input					ACLK;    // Synchronous clock
	input					ARESETN; // System reset, active low
	// slave in interface
	output	reg				S_AXIS_TREADY;  // Ready to accept data in
	input	[31 : 0]		S_AXIS_TDATA;   // Data in
	input					S_AXIS_TLAST;   // Optional data in qualifier
	input					S_AXIS_TVALID;  // Data in is valid
	// master out interface
	output	reg				M_AXIS_TVALID;  // Data out is valid
	output	reg [31 : 0]	M_AXIS_TDATA;   // Data Out
	output	reg				M_AXIS_TLAST;   // Optional data out qualifier
	input					M_AXIS_TREADY;  // Connected slave device is ready to accept data out

	//----------------------------------------
	// Implementation Section
	//----------------------------------------
	// Total number of input data.
	localparam INPUT_WORDS_A         = m*n;
	localparam INPUT_WORDS_B         = n;
	localparam NUMBER_OF_OUTPUT_WORDS = m;
	localparam NUMBER_OF_INPUT_WORDS  = INPUT_WORDS_A + INPUT_WORDS_B;

	// RAM parameters for assignment 1
	localparam A_depth_bits   = $clog2(INPUT_WORDS_A);  	// 1024 elements (A is a 32x32 matrix)
	localparam B_depth_bits   = $clog2(INPUT_WORDS_B); 	// 32 elements (B is a 32x1 matrix)
	localparam RES_depth_bits = $clog2(NUMBER_OF_OUTPUT_WORDS);	// 32 elements (RES is a 2x1 matrix)

	// wires (or regs) to connect to RAMs and matrix_multiply_0 for assignment 1
	// those which are assigned in an always block of myip_v1_0 shoud be changes to reg.
	reg								A_write_en;				// myip_v1_0 -> A_RAM. To be assigned within myip_v1_0. Possibly reg.
	reg		[A_depth_bits-1:0] 		A_write_address;		// myip_v1_0 -> A_RAM. To be assigned within myip_v1_0. Possibly reg.
	reg		[width-1:0]				A_write_data_in;		// myip_v1_0 -> A_RAM. To be assigned within myip_v1_0. Possibly reg.
	wire							A_read_en;				// matrix_multiply_0 -> A_RAM.
	wire	[A_depth_bits-1:0] 		A_read_address;			// matrix_multiply_0 -> A_RAM.
	wire	[width-1:0]	 			A_read_data_out;		// A_RAM -> matrix_multiply_0.
	reg								B_write_en;				// myip_v1_0 -> B_RAM. To be assigned within myip_v1_0. Possibly reg.
	reg		[B_depth_bits-1:0] 		B_write_address;		// myip_v1_0 -> B_RAM. To be assigned within myip_v1_0. Possibly reg.
	reg		[width-1:0] 			B_write_data_in;		// myip_v1_0 -> B_RAM. To be assigned within myip_v1_0. Possibly reg.
	wire							B_read_en;				// matrix_multiply_0 -> B_RAM.
	wire	[B_depth_bits-1:0] 		B_read_address;			// matrix_multiply_0 -> B_RAM.
	wire	[width-1:0] 			B_read_data_out;		// B_RAM -> matrix_multiply_0.
	wire							RES_write_en;			// matrix_multiply_0 -> RES_RAM.
	wire	[RES_depth_bits-1:0]	RES_write_address;		// matrix_multiply_0 -> RES_RAM.
	wire	[width-1:0] 			RES_write_data_in;		// matrix_multiply_0 -> RES_RAM.
	reg								RES_read_en;  			// myip_v1_0 -> RES_RAM. To be assigned within myip_v1_0. Possibly reg.
	reg		[RES_depth_bits-1:0] 	RES_read_address;		// myip_v1_0 -> RES_RAM. To be assigned within myip_v1_0. Possibly reg.
	wire	[width-1:0] 			RES_read_data_out;		// RES_RAM -> myip_v1_0

	// wires (or regs) to connect to matrix_multiply for assignment 1
	reg		Start; 								// myip_v1_0 -> matrix_multiply_0. To be assigned within myip_v1_0. Possibly reg.
	wire	Done;								// matrix_multiply_0 -> myip_v1_0.

	// Define the states of state machine (one hot encoding)
	localparam IDLE          = 5'b00001;
	localparam READ_INPUTS_A = 5'b00010;
	localparam READ_INPUTS_B = 5'b00100;
	localparam COMPUTE       = 5'b01000;
	localparam WRITE_OUTPUTS = 5'b10000;
	reg [4:0] state;

	reg RES_read_en_dly;
	reg [RES_depth_bits-1:0] RES_read_address_dly;

	always_ff @(posedge ACLK)
	begin
	// implemented as a single-always Moore machine
	// a Mealy machine that asserts S_AXIS_TREADY and captures S_AXIS_TDATA etc can save a clock cycle

		/****** Synchronous reset (active low) ******/
		if (~ARESETN)
		begin
			// CAUTION: make sure your reset polarity is consistent with the system reset polarity
			S_AXIS_TREADY 		 <= 1'b0;
			M_AXIS_TLAST  		 <= 1'b0;
			M_AXIS_TVALID 		 <= 1'b0;
			M_AXIS_TDATA       	 <= {32{1'b0}};
			A_write_en 			 <= 1'b0;
			A_write_address 	 <= {A_depth_bits{1'b0}};
			A_write_data_in 	 <= {width{1'b0}};
			B_write_en 			 <= 1'b0;
			B_write_address 	 <= {B_depth_bits{1'b0}};
			B_write_data_in 	 <= {width{1'b0}};
			RES_read_en 		 <= 1'b0;
			RES_read_address 	 <= {RES_depth_bits{1'b0}};
			Start				 <= 1'b0;

			state       		 <= IDLE;

			RES_read_en_dly 		<= 1'b0;
			RES_read_address_dly <= {RES_depth_bits{1'b0}};
        end
		else
		begin
			// NOTE explicit latch prevention
			S_AXIS_TREADY 		 <= 1'b0;
			M_AXIS_TLAST  		 <= 1'b0;
			M_AXIS_TVALID 		 <= 1'b0;
			M_AXIS_TDATA       	 <= {32{1'b0}};
			A_write_en 			 <= 1'b0;
			A_write_address 	 <= {A_depth_bits{1'b0}};
			A_write_data_in 	 <= {width{1'b0}};
			B_write_en 			 <= 1'b0;
			B_write_address 	 <= {B_depth_bits{1'b0}};
			B_write_data_in 	 <= {width{1'b0}};
			RES_read_en 		 <= 1'b0;
			RES_read_address 	 <= {RES_depth_bits{1'b0}};
			Start				 <= 1'b0;

			RES_read_en_dly 		<= RES_read_en;
			RES_read_address_dly <= RES_read_address;

			case (state)

				IDLE:
				begin
					if (S_AXIS_TVALID)
					begin
					S_AXIS_TREADY 	 <= 1'b1;

					state       	 <= READ_INPUTS_A;

					A_write_en 		 <= 1'b1;
					A_write_address  <= {A_depth_bits{1'b0}};
					A_write_data_in  <= S_AXIS_TDATA[width-1:0];
					end
				end

				READ_INPUTS_A:
				begin
					S_AXIS_TREADY 	<= 1'b1;
					A_write_address <= A_write_address;

					if (S_AXIS_TVALID)
					begin
						if (A_write_address == (INPUT_WORDS_A - 1))
						begin
							state <= READ_INPUTS_B;

							B_write_en 		<= 1'b1;
							B_write_address <= 1'b0;
							B_write_data_in <= S_AXIS_TDATA[width-1:0];
						end
						else
						begin
							A_write_en 		<= 1'b1;
							A_write_address <= A_write_address + 1'b1;
							A_write_data_in <= S_AXIS_TDATA[width-1:0];
						end
					end
				end

				READ_INPUTS_B:
				begin
					S_AXIS_TREADY 	<= 1'b1;
					B_write_address <= B_write_address;

					if (S_AXIS_TVALID)
					begin
						if (B_write_address == (INPUT_WORDS_B - 1))
						begin
							state <= COMPUTE;
							Start <= 1'b1;
						end
						else
						begin
							B_write_en 		<= 1'b1;
							B_write_address <= B_write_address + 1'b1;
							B_write_data_in <= S_AXIS_TDATA[width-1:0];
						end
					end
				end

				COMPUTE:
				begin
					if (Done)
					begin
					state			 <= WRITE_OUTPUTS;

					RES_read_en 		 <= 1'b1;
					RES_read_address 	 <= {RES_depth_bits{1'b0}};
					end
				end

				WRITE_OUTPUTS:
				begin
					M_AXIS_TVALID <= M_AXIS_TVALID;
					M_AXIS_TDATA  <= M_AXIS_TDATA;
					RES_read_en   <= 1'b1;

					if (M_AXIS_TREADY);
					begin
						if (RES_read_address != (NUMBER_OF_OUTPUT_WORDS - 1)) RES_read_address <= RES_read_address + 1;

						if (RES_read_en_dly)
						begin
							M_AXIS_TVALID 	<= 1'b1;
							M_AXIS_TDATA 	<= {{(32-width){1'b0}}, RES_read_data_out};
							if (RES_read_address_dly == (NUMBER_OF_OUTPUT_WORDS - 1))
							begin
								M_AXIS_TLAST <= 1'b1;
								state 		 <= IDLE;
							end
						end
					end
				end

			endcase
		end
	end

	// Connection to sub-modules / components for assignment 1

	memory_RAM
	#(
		.width(width),
		.depth_bits(A_depth_bits)
	) A_RAM
	(
		.clk(ACLK),
		.write_en(A_write_en),
		.write_address(A_write_address),
		.write_data_in(A_write_data_in),
		.read_en(A_read_en),
		.read_address(A_read_address),
		.read_data_out(A_read_data_out)  // Output
	);


	memory_RAM
	#(
		.width(width),
		.depth_bits(B_depth_bits)
	) B_RAM
	(
		.clk(ACLK),
		.write_en(B_write_en),
		.write_address(B_write_address),
		.write_data_in(B_write_data_in),
		.read_en(B_read_en),
		.read_address(B_read_address),
		.read_data_out(B_read_data_out)  // Output
	);


	memory_RAM
	#(
		.width(width),
		.depth_bits(RES_depth_bits)
	) RES_RAM
	(
		.clk(ACLK),
		.write_en(RES_write_en),
		.write_address(RES_write_address),
		.write_data_in(RES_write_data_in),
		.read_en(RES_read_en),
		.read_address(RES_read_address),
		.read_data_out(RES_read_data_out)  // Output
	);

	matrix_multiply
	#(
		.width(width),
		.A_depth_bits(A_depth_bits),
		.B_depth_bits(B_depth_bits),
		.RES_depth_bits(RES_depth_bits)
	) matrix_multiply_0
	(
		.clk(ACLK),
		.aresetn(ARESETN),
		.Start(Start),
		.Done(Done),

		.A_read_en(A_read_en),
		.A_read_address(A_read_address),
		.A_read_data_out(A_read_data_out),

		.B_read_en(B_read_en),
		.B_read_address(B_read_address),
		.B_read_data_out(B_read_data_out),

		.RES_write_en(RES_write_en),
		.RES_write_address(RES_write_address),
		.RES_write_data_in(RES_write_data_in)
	);

endmodule


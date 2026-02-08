`timescale 1ns / 1ps

/*
----------------------------------------------------------------------------------
--	Modified stress testbench with random handshake interference
--	Based on original testbench by Rajesh C Panicker, NUS
--  Description : Stress-testing testbench for Matrix Multiplication AXI Stream Coprocessor
--                with random delays on TVALID and TREADY signals.
----------------------------------------------------------------------------------
*/

module tb_myip_v1_0_edgecase(

    );

    reg                          ACLK = 0;    // Synchronous clock
    reg                          ARESETN; // System reset, active low
    // slave in interface
    wire                         S_AXIS_TREADY;  // Ready to accept data in
    reg      [31 : 0]            S_AXIS_TDATA;   // Data in
    reg                          S_AXIS_TLAST;   // Optional data in qualifier
    reg                          S_AXIS_TVALID;  // Data in is valid
    // master out interface
    wire                         M_AXIS_TVALID;  // Data out is valid
    wire     [31 : 0]            M_AXIS_TDATA;   // Data out
    wire                         M_AXIS_TLAST;   // Optional data out qualifier
    reg                          M_AXIS_TREADY;  // Connected slave device is ready to accept data out

    localparam m = 32;
    localparam n = 32;
    localparam NUMBER_OF_INPUT_WORDS  = (m*n) + n;  // length of an input vector
    localparam NUMBER_OF_OUTPUT_WORDS  = m;  // length of an input vector
    localparam NUMBER_OF_TEST_VECTORS  = 10;  // number of such test vectors (cases)
    localparam width  = 8;  // width of an input vector
    
    // Stress test parameters
    localparam TVALID_DELAY_PROB = 30;  // 30% chance to delay S_AXIS_TVALID
    localparam TREADY_DELAY_PROB = 30;  // 30% chance to delay M_AXIS_TREADY
    localparam MAX_DELAY_CYCLES = 5;    // Maximum random delay cycles

    myip_v1_0 #(
        .m(m),
        .n(n),
        .width(width)
    ) U1 (
        .ACLK(ACLK),
        .ARESETN(ARESETN),
        .S_AXIS_TREADY(S_AXIS_TREADY),
        .S_AXIS_TDATA(S_AXIS_TDATA),
        .S_AXIS_TLAST(S_AXIS_TLAST),
        .S_AXIS_TVALID(S_AXIS_TVALID),
        .M_AXIS_TVALID(M_AXIS_TVALID),
        .M_AXIS_TDATA(M_AXIS_TDATA),
        .M_AXIS_TLAST(M_AXIS_TLAST),
        .M_AXIS_TREADY(M_AXIS_TREADY)
    );

    reg [width-1:0] test_input_memory [0:NUMBER_OF_TEST_VECTORS*NUMBER_OF_INPUT_WORDS-1];
    reg [width-1:0] test_result_expected_memory [0:NUMBER_OF_TEST_VECTORS*NUMBER_OF_OUTPUT_WORDS-1];
    reg [width-1:0] result_memory [0:NUMBER_OF_TEST_VECTORS*NUMBER_OF_OUTPUT_WORDS-1];

    integer word_cnt, test_case_cnt, delay_cycles, i;
    integer random_value;
    reg success = 1'b1;
    reg M_AXIS_TLAST_prev = 1'b0;

    always@(posedge ACLK)
        M_AXIS_TLAST_prev <= M_AXIS_TLAST;

    always #50 ACLK = ~ACLK;

    initial
    begin
        $display("=== Starting Stress Test with Random Handshake Interference ===");
        $display("Loading Memory.");
        $readmemh("test_input.mem", test_input_memory);
        $readmemh("test_result_expected.mem", test_result_expected_memory);
        
        #25
        ARESETN = 1'b0;
        S_AXIS_TVALID = 1'b0;
        S_AXIS_TLAST = 1'b0;
        M_AXIS_TREADY = 1'b0;

        #100
        ARESETN = 1'b1;
        $display("Reset released. Beginning stress test...");

        for(test_case_cnt=0; test_case_cnt < NUMBER_OF_TEST_VECTORS; test_case_cnt=test_case_cnt+1)
        begin
            $display("Test Vector %0d/%0d", test_case_cnt+1, NUMBER_OF_TEST_VECTORS);

        //// Input with random TVALID delays
            word_cnt=0;
            
            // First word setup
            S_AXIS_TDATA = test_input_memory[word_cnt+test_case_cnt*NUMBER_OF_INPUT_WORDS];
            S_AXIS_TLAST = (word_cnt == NUMBER_OF_INPUT_WORDS-1) ? 1'b1 : 1'b0;
            
            // Randomly delay first TVALID assertion
            random_value = $random % 100;
            if (random_value < TVALID_DELAY_PROB) begin
                delay_cycles = ($random % MAX_DELAY_CYCLES) + 1;
                $display("  Delaying initial S_AXIS_TVALID by %0d cycles", delay_cycles);
                for(i=0; i<delay_cycles; i=i+1) @(posedge ACLK);
            end
            
            S_AXIS_TVALID = 1'b1;
            word_cnt=word_cnt+1;

            while(word_cnt < NUMBER_OF_INPUT_WORDS)
            begin
                if(S_AXIS_TREADY && S_AXIS_TVALID)
                begin
                    // @(posedge ACLK);//TODO: dont use event control in synthesizable code
                    
                    // Randomly drop TVALID (simulate gaps in data availability)
                    random_value = $random % 100;
                    if (random_value < TVALID_DELAY_PROB && word_cnt < NUMBER_OF_INPUT_WORDS) begin
                        delay_cycles = ($urandom % MAX_DELAY_CYCLES) + 1;
                        $display("  Dropping S_AXIS_TVALID for %0d cycles at word %0d", delay_cycles, word_cnt);
                        S_AXIS_TVALID = 1'b0;
                        for(i=0; i<delay_cycles; i=i+1) @(posedge ACLK); // TODO: dont use event control in synthesizable code
                        S_AXIS_TVALID = 1'b1;
                    end
                    
                    // Set next data
                    S_AXIS_TDATA = test_input_memory[word_cnt+test_case_cnt*NUMBER_OF_INPUT_WORDS];
                    S_AXIS_TLAST = (word_cnt == NUMBER_OF_INPUT_WORDS-1) ? 1'b1 : 1'b0;
                    word_cnt=word_cnt+1;
                    @(posedge ACLK);
                end
                else
                begin
                    @(posedge ACLK);
                end
            end
            
            @(posedge ACLK);
            S_AXIS_TVALID = 1'b0;
            S_AXIS_TLAST = 1'b0;
            
        /// Output (no testing yet)
		// Note: result_memory is not written at a clock edge, which is fine as it is just a testbench construct and not actual hardware
			word_cnt = 0;
			M_AXIS_TREADY = 1'b1;	// we are now ready to receive data
			while(M_AXIS_TLAST | ~M_AXIS_TLAST_prev) // receive data until the falling edge of M_AXIS_TLAST
			begin
				if(M_AXIS_TVALID)
				begin
					result_memory[word_cnt+test_case_cnt*NUMBER_OF_OUTPUT_WORDS] = M_AXIS_TDATA;
					word_cnt = word_cnt+1;
				end
				@(posedge ACLK);
			end						// receive loop
			M_AXIS_TREADY = 1'b0;	// not ready to receive data from the co-processor anymore.
		end							// next test vector


        // /// Output with random TREADY delays
        //     word_cnt = 0;
            
        //     // Randomly delay initial TREADY assertion
        //     random_value = $random % 100;
        //     if (random_value < TREADY_DELAY_PROB) begin
        //         delay_cycles = ($random % MAX_DELAY_CYCLES) + 1;
        //         $display("  Delaying initial M_AXIS_TREADY by %0d cycles", delay_cycles);
        //         for(i=0; i<delay_cycles; i=i+1) @(posedge ACLK); // TODO: dont use event control in synthesizable code
        //     end
            
        //     M_AXIS_TREADY = 1'b1;
            
        //     while(M_AXIS_TLAST | ~M_AXIS_TLAST_prev)
        //     begin
        //         if(M_AXIS_TVALID && M_AXIS_TREADY)
        //         begin
        //             result_memory[word_cnt+test_case_cnt*NUMBER_OF_OUTPUT_WORDS] = M_AXIS_TDATA;
        //             word_cnt = word_cnt+1;
                    
        //             @(posedge ACLK);
                    
        //             // Randomly drop TREADY (simulate back-pressure)
        //             random_value = $random % 100;
        //             if (random_value < TREADY_DELAY_PROB && (M_AXIS_TLAST | ~M_AXIS_TLAST_prev)) begin
        //                 delay_cycles = ($random % MAX_DELAY_CYCLES) + 1;
        //                 $display("  Dropping M_AXIS_TREADY for %0d cycles at word %0d", delay_cycles, word_cnt);
        //                 M_AXIS_TREADY = 1'b0;
        //                 for(i=0; i<delay_cycles; i=i+1) @(posedge ACLK); // TODO: dont use event control in synthesizable code
        //                 M_AXIS_TREADY = 1'b1;
        //             end
        //         end
        //         else
        //         begin
        //             @(posedge ACLK);
        //         end
        //     end
            
        //     M_AXIS_TREADY = 1'b0;
        //     $display("  Completed test vector %0d", test_case_cnt+1);
        // end

        // Checking correctness of results
        $display("\n=== Verifying Results ===");
        for(word_cnt=0; word_cnt < NUMBER_OF_TEST_VECTORS*NUMBER_OF_OUTPUT_WORDS; word_cnt=word_cnt+1)
        begin
            if(result_memory[word_cnt] != test_result_expected_memory[word_cnt]) begin
                $display("MISMATCH at index %0d: Got 0x%h, Expected 0x%h", 
                         word_cnt, result_memory[word_cnt], test_result_expected_memory[word_cnt]);
                success = 1'b0;
            end
        end
        
        if(success)
            $display("\n*** STRESS TEST PASSED ***");
        else
            $display("\n*** STRESS TEST FAILED ***");

        $finish;
    end

endmodule
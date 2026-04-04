`timescale 1ps/

module project_ip
# (
    parameter integer  N = 32;
    parameter integer  DATA_WIDTH = 32;
    localparam integer PTR_WIDTH = $clog2(N);
)
(
	input wire                      aclk,
	input wire                      aresetn,
	input wire                      s_axis_tvalid,
	output reg                      s_axis_tready,
	input wire [DATA_WIDTH-1:0]     s_axis_tdata,
	input wire                      s_axis_tlast,
	output reg                      m_axis_tvalid,
	input wire                      m_axis_tready
    output reg [DATA_WIDTH-1:0]     m_axis_tdata,
    output reg                      m_axis_tlast
);

    typedef enum logic [4:0] {
        IDLE            = 5'b00001,
        READ            = 5'b00010,
        WRITE_INIT      = 5'b00100,
        WRITE           = 5'b01000,
        WRITE_LAST      = 5'b10000
    } state_t;



    reg [PTR_WIDTH-1:0] wptr;
    reg [PTR_WIDTH-1:0] rptr;
    reg [1:0] dly;

    state_t state;

    // m_RAM 1 inputs and outputs
    wire                        mram1_en;
    reg                         mram1_wr;
    reg     [PTR_WIDTH-1:0]     mram1_addr;
    wire    [DATA_WIDTH-1:0]    mram1_di;
    wire    [DATA_WIDTH-1:0]    mram1_do;

    reg mram1_en_intrnl;
    reg mram1_addr_dly;


    // wire assignments
    wire s_axis_en = s_axis_tvalid & s_axis_tready;
    wire m_axis_en = m_axis_tready & m_axis_tvalid;

    mram1_en = (state == WRITE) ? m_axis_en : mram1_en_intrnl;

    always_ff @(posedge aclk)
    begin
        if (~aresetn)
        begin
            mram_addr_dly <= '0;
        else
            if (mram1_en)
            begin
                mram_addr_dly <= mram1_addr;
            end
        end
    end

    always_ff @(posege clk)
    begin
        if (~aresetn)
        begin
            s_axis_tready   <= 1'b0;
            m_axis_tvalid   <= 1'b0;
            m_axis_tdata    <= '0;
            m_axis_tlast    <= 1'b0;

            mram1_en_intrnl <= 1'b0;
            mram1_wr        <= 1'b0;
            mram1_addr      <= '0;

            state <= IDLE;
        end
        else
        begin
            s_axis_tready <= 1'b0;
            m_axis_tvalid <= 1'b0;
            m_axis_tdata <= '0;
            m_axis_tlast <= 1'b0;

            mram1_en_intrnl <= 1'b0;
            mram1_wr        <= 1'b0;
            mram1_addr      <= '0;
            mram1_di        <= '0;

            case (state)
                IDLE:
                begin
                    s_axis_tready <= 1'b1;

                    wptr <= '0;
                    rptr <= '0;
                    dly <=  '0;

                    state <= READ;
                end

                READ:
                begin
                    s_axis_tready   <= 1'b1;

                    mram1_wr        <= 1'b1;

                    if (s_axis_en)
                    begin
                        mram1_en_intrnl <= 1'b1;
                        mram1_addr      <= wptr;
                        mram1_di        <= s_axis_tdata;

                        if ((wptr == (N - 1)) | s_axis_tlast)
                        begin
                            s_axis_tready <= 1'b0;

                            rptr    <= '0;
                            dly     <= '0;

                            state   <= WRITE_INIT;
                        end
                        else
                        begin
                            wptr <= wptr + 1;
                        end
                    end
                end

                WRITE_INIT:
                begin
                    mram1_wr <= 1'b0;

                    if (rptr != wptr)
                    begin
                        mram_en_intrnl  <= 1'b1;
                        mram1_addr      <= rptr;

                        rptr <= rptr + 1;
                    end

                    if (dly == 2'd2)
                    begin
                        m_axis_tvalid <= 1'b1;
                        m_axis_tdata <= mram1_do;

                        dly <= '0;

                        state <= WRITE;
                    end
                    else
                    begin
                        dly <= dly + 1;
                    end
                end

                WRITE:
                begin
                    m_axis_tvalid   <= 1'b1;
                    m_axis_tdata    <= m_axis_tdata;
                    m_axis_tlast    <= 1'b0;

                    mram1_wr    <= 1'b0;
                    mram1_addr  <= mram1_addr;

                    if (m_axis_en)
                    begin
                        m_axis_tdata <= mram1_do;

                        mram1_addr <= rptr;

                        if (rptr != wptr)
                        begin
                            rptr <= rptr + 1;
                        end

                        if (mram_addr_dly == wptr)
                        begin
                            m_axis_tlast <= 1'b1;

                            mram1_wr    <= '0;
                            mram1_addr  <= '0;

                            wptr <= '0;
                            rptr <= '0;

                            state <= WRITE_LAST;
                        end
                    end
                end

                WRITE_LAST:
                begin
                    m_axis_tvalid   <= 1'b1;
                    m_axis_tdata    <= m_axis_tdata;
                    m_axis_tlast    <= m_axis_tlast;

                    if (m_axis_en)
                    begin
                        m_axis_tvalid <= 1'b0;
                        m_axis_tdata <= '0;
                        m_axis_tlast <= 1'b0;

                        state <= IDLE;
                    end
                end

                default:
                begin
                    state <= IDLE;
                end
            endcase
        end
    end

    memory_RAM
    #(
        .WIDTH (DATA_WIDTH),
        .DEPTH_BITS (PTR_WIDTH)
    )
    mram1
    (
        .clk    (aclk),
        .en     (mram1_en),
        .wr     (mram1_wr),
        .addr   (mram1_addr),
        .di     (mram1_di),
        .do     (mram1_do)
    )

endmodule
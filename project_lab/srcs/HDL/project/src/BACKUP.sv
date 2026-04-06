// WRITE_INIT:
                // begin
                //     mram1_wr <= 1'b0;

                //     if (rptr != wptr)
                //     begin
                //         mram1_en_intrnl  <= 1'b1;
                //         mram1_addr      <= rptr;

                //         rptr <= rptr + 1;
                //     end

                //     if (dly == 2'd2)
                //     begin
                //         m_axis_tvalid <= 1'b1;
                //         m_axis_tdata <= mram1_do;

                //         dly <= '0;

                //         state <= WRITE;
                //     end
                //     else
                //     begin
                //         dly <= dly + 1;
                //     end
                // end

                // WRITE:
                // begin
                //     m_axis_tvalid   <= 1'b1;
                //     m_axis_tdata    <= m_axis_tdata;
                //     m_axis_tlast    <= 1'b0;

                //     mram1_wr    <= 1'b0;
                //     mram1_addr  <= mram1_addr;

                //     if (m_axis_en)
                //     begin
                //         m_axis_tdata <= mram1_do;

                //         mram1_addr <= rptr;

                //         if (rptr != wptr)
                //         begin
                //             rptr <= rptr + 1;
                //         end

                //         if (mram1_addr_dly == wptr)
                //         begin
                //             m_axis_tlast <= tlast_recvd;

                //             mram1_wr    <= '0;
                //             mram1_addr  <= '0;
                //             tlast_recvd <= 1'b0;

                //             wptr <= '0;
                //             rptr <= '0;

                //             state <= WRITE_LAST;
                //         end
                //     end
                // end

                // WRITE_LAST:
                // begin
                //     m_axis_tvalid   <= 1'b1;
                //     m_axis_tdata    <= m_axis_tdata;
                //     m_axis_tlast    <= m_axis_tlast;

                //     if (m_axis_en)
                //     begin
                //         m_axis_tvalid <= 1'b0;
                //         m_axis_tdata <= '0;
                //         m_axis_tlast <= 1'b0;

                //         if (m_axis_tlast)
                //         begin
                //             state <= IDLE;
                //         end
                //         else
                //         begin
                //             state <= READ;
                //         end
                //     end
                // end
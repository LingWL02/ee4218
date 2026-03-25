
cmake_minimum_required(VERSION 3.15)
project(bsp)

# Use CMAKE_LIBRARY_PATH in link_directories
link_directories(${CMAKE_LIBRARY_PATH})
        
set(DRIVER_TARGETS llfifo;tmrctr;scugic;zdma;sysmonpsu;axipmon;coresightps_dcc;resetps;clockps;csudma;ddrcpsu;gpiops;iicps;ipipsu;qspipsu;rtcpsu;ttcps;uartps;wdtps)
set(DRIVER_LOCATIONS C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/llfifo_v5_9;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/tmrctr_v4_13;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/scugic_v5_5;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/zdma_v1_20;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/sysmonpsu_v3_1;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/axipmon_v6_12;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/coresightps_dcc_v1_12;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/resetps_v1_7;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/clockps_v1_6;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/csudma_v1_17;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/ddrcpsu_v1_8;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/gpiops_v3_14;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/iicps_v3_21;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/ipipsu_v2_17;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/qspipsu_v1_21;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/rtcpsu_v1_15;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/ttcps_v3_21;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/uartps_v3_16;C:/Xilinx/2025.1/Vitis/data/embeddedsw/XilinxProcessorIPLib/drivers/wdtps_v3_8)

list(LENGTH DRIVER_TARGETS no_of_drivers)
set(index 0)

while(${index} LESS ${no_of_drivers})
    list(GET DRIVER_TARGETS ${index} drv)
    list(GET DRIVER_LOCATIONS ${index} drv_dir)
    set(exp_dir "${drv_dir}/examples")
    add_custom_target(
        ${drv}_example ALL
        COMMAND lopper -f -O C:/Users/dkhar/OneDrive/Documents/GitHub/ee4218/platform/psu_cortexa53_0/standalone_psu_cortexa53_0/bsp/libsrc/${drv} C:/Users/dkhar/OneDrive/Documents/GitHub/ee4218/platform/psu_cortexa53_0/standalone_psu_cortexa53_0/bsp/hw_artifacts/psu_cortexa53_0_baremetal.dts -- bmcmake_metadata_xlnx psu_cortexa53_0 ${exp_dir} drvcmake_metadata
        BYPRODUCTS x${drv}_exlist.yaml)
    MATH(EXPR index "${index}+1")
endwhile()

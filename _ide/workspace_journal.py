# 2026-03-18T16:10:52.510081600
import vitis

client = vitis.create_client()
client.set_workspace(path="ee4218")

platform = client.get_component(name="platform")
status = platform.build()

comp = client.get_component(name="lab_2")
comp.build()

status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../lab3/srcs/fifo/hw/lab3_fifo.xsa")

status = platform.build()

status = client.add_platform_repos(platform=["c:\Users\dkhar\OneDrive\Documents\GitHub\ee4218\platform"])

status = client.add_platform_repos(platform=["c:\Users\dkhar\OneDrive\Documents\GitHub\ee4218\platform"])

status = platform.build()

vitis.dispose()


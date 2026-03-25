# 2026-03-18T15:27:10.143521
import vitis

client = vitis.create_client()
client.set_workspace(path="ee4218")

advanced_options = client.create_advanced_options_dict(dt_overlay="0")

platform = client.create_platform_component(name = "platform",hw_design = "$COMPONENT_LOCATION/../lab2/hw/lab2.xsa",os = "standalone",cpu = "psu_cortexa53_0",domain_name = "standalone_psu_cortexa53_0",generate_dtb = False,advanced_options = advanced_options,architecture = "64-bit",compiler = "gcc")

platform = client.get_component(name="platform")
status = platform.build()

comp = client.get_component(name="xuartps_hello_world_example")
comp.build()

comp = client.create_app_component(name="lab_2",platform = "$COMPONENT_LOCATION/../platform/export/platform/platform.xpfm",domain = "standalone_psu_cortexa53_0")

comp = client.get_component(name="lab_2")
status = comp.import_files(from_loc="", files=["C:\Users\dkhar\OneDrive\Documents\GitHub\ee4218\lab2\srcs\lab2.c", "C:\Users\dkhar\OneDrive\Documents\GitHub\ee4218\lab2\srcs\lab2.h"])

status = platform.build()

comp = client.get_component(name="lab_2")
comp.build()

vitis.dispose()


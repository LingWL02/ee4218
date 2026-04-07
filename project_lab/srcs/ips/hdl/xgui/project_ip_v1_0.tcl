# Definitional proc to organize widgets for parameters.
proc init_gui { IPINST } {
  ipgui::add_param $IPINST -name "Component_Name"
  #Adding Page
  set Page_0 [ipgui::add_page $IPINST -name "Page 0"]
  ipgui::add_param $IPINST -name "DATA_WIDTH" -parent ${Page_0}
  ipgui::add_param $IPINST -name "INVALID_TOKEN" -parent ${Page_0}
  ipgui::add_param $IPINST -name "N_W_HIDDEN" -parent ${Page_0}
  ipgui::add_param $IPINST -name "N_W_OUTPUT" -parent ${Page_0}
  ipgui::add_param $IPINST -name "N_X" -parent ${Page_0}


}

proc update_PARAM_VALUE.DATA_WIDTH { PARAM_VALUE.DATA_WIDTH } {
	# Procedure called to update DATA_WIDTH when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.DATA_WIDTH { PARAM_VALUE.DATA_WIDTH } {
	# Procedure called to validate DATA_WIDTH
	return true
}

proc update_PARAM_VALUE.INVALID_TOKEN { PARAM_VALUE.INVALID_TOKEN } {
	# Procedure called to update INVALID_TOKEN when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.INVALID_TOKEN { PARAM_VALUE.INVALID_TOKEN } {
	# Procedure called to validate INVALID_TOKEN
	return true
}

proc update_PARAM_VALUE.N_W_HIDDEN { PARAM_VALUE.N_W_HIDDEN } {
	# Procedure called to update N_W_HIDDEN when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.N_W_HIDDEN { PARAM_VALUE.N_W_HIDDEN } {
	# Procedure called to validate N_W_HIDDEN
	return true
}

proc update_PARAM_VALUE.N_W_OUTPUT { PARAM_VALUE.N_W_OUTPUT } {
	# Procedure called to update N_W_OUTPUT when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.N_W_OUTPUT { PARAM_VALUE.N_W_OUTPUT } {
	# Procedure called to validate N_W_OUTPUT
	return true
}

proc update_PARAM_VALUE.N_X { PARAM_VALUE.N_X } {
	# Procedure called to update N_X when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.N_X { PARAM_VALUE.N_X } {
	# Procedure called to validate N_X
	return true
}


proc update_MODELPARAM_VALUE.N_W_HIDDEN { MODELPARAM_VALUE.N_W_HIDDEN PARAM_VALUE.N_W_HIDDEN } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.N_W_HIDDEN}] ${MODELPARAM_VALUE.N_W_HIDDEN}
}

proc update_MODELPARAM_VALUE.N_W_OUTPUT { MODELPARAM_VALUE.N_W_OUTPUT PARAM_VALUE.N_W_OUTPUT } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.N_W_OUTPUT}] ${MODELPARAM_VALUE.N_W_OUTPUT}
}

proc update_MODELPARAM_VALUE.N_X { MODELPARAM_VALUE.N_X PARAM_VALUE.N_X } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.N_X}] ${MODELPARAM_VALUE.N_X}
}

proc update_MODELPARAM_VALUE.DATA_WIDTH { MODELPARAM_VALUE.DATA_WIDTH PARAM_VALUE.DATA_WIDTH } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.DATA_WIDTH}] ${MODELPARAM_VALUE.DATA_WIDTH}
}

proc update_MODELPARAM_VALUE.INVALID_TOKEN { MODELPARAM_VALUE.INVALID_TOKEN PARAM_VALUE.INVALID_TOKEN } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.INVALID_TOKEN}] ${MODELPARAM_VALUE.INVALID_TOKEN}
}


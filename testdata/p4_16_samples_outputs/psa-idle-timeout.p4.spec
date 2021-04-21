
struct EMPTY {
	bit<32> psa_ingress_parser_input_metadata_ingress_port
	bit<32> psa_ingress_parser_input_metadata_packet_path
	bit<32> psa_egress_parser_input_metadata_egress_port
	bit<32> psa_egress_parser_input_metadata_packet_path
	bit<32> psa_ingress_input_metadata_ingress_port
	bit<32> psa_ingress_input_metadata_packet_path
	bit<64> psa_ingress_input_metadata_ingress_timestamp
	bit<8> psa_ingress_input_metadata_parser_error
	bit<8> psa_ingress_output_metadata_class_of_service
	bit<8> psa_ingress_output_metadata_clone
	bit<16> psa_ingress_output_metadata_clone_session_id
	bit<8> psa_ingress_output_metadata_drop
	bit<8> psa_ingress_output_metadata_resubmit
	bit<32> psa_ingress_output_metadata_multicast_group
	bit<32> psa_ingress_output_metadata_egress_port
	bit<8> psa_egress_input_metadata_class_of_service
	bit<32> psa_egress_input_metadata_egress_port
	bit<32> psa_egress_input_metadata_packet_path
	bit<16> psa_egress_input_metadata_instance
	bit<64> psa_egress_input_metadata_egress_timestamp
	bit<8> psa_egress_input_metadata_parser_error
	bit<32> psa_egress_deparser_input_metadata_egress_port
	bit<8> psa_egress_output_metadata_clone
	bit<16> psa_egress_output_metadata_clone_session_id
	bit<8> psa_egress_output_metadata_drop
}
metadata instanceof EMPTY

struct a1_arg_t {
	bit<48> param
}

struct a2_arg_t {
	bit<16> param
}

action NoAction args none {
	return
}

action a1 args instanceof a1_arg_t {
	mov h.dstAddr t.param
	return
}

action a2 args instanceof a2_arg_t {
	mov h.etherType t.param
	return
}

table tbl_idle_timeout {
	key {
		h.srcAddr exact
	}
	actions {
		NoAction
		a1
		a2
	}
	default_action NoAction args none 
	size 0
}


table tbl_no_idle_timeout {
	key {
		h.srcAddr2 exact
	}
	actions {
		NoAction
		a1
		a2
	}
	default_action NoAction args none 
	size 0
}


table tbl_no_idle_timeout_prop {
	key {
		h.srcAddr2 exact
	}
	actions {
		NoAction
		a1
		a2
	}
	default_action NoAction args none 
	size 0
}


apply {
	rx m.psa_ingress_input_metadata_ingress_port
	mov m.psa_ingress_output_metadata_drop 0x0
	extract h
	table tbl_idle_timeout
	table tbl_no_idle_timeout
	table tbl_no_idle_timeout_prop
	jmpneq LABEL_DROP m.psa_ingress_output_metadata_drop 0x0
	emit h
	tx m.psa_ingress_output_metadata_egress_port
	LABEL_DROP : drop
}



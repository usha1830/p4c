
struct ethernet_t {
	bit<48> dstAddr
	bit<48> srcAddr
	bit<16> etherType
}

struct vlan_tag_h {
	bit<16> pcp_cfi_vid
	bit<16> ether_type
}

struct psa_ingress_output_metadata_t {
	bit<8> class_of_service
	bit<8> clone
	bit<16> clone_session_id
	bit<8> drop
	bit<8> resubmit
	bit<32> multicast_group
	bit<32> egress_port
}

struct psa_egress_output_metadata_t {
	bit<8> clone
	bit<16> clone_session_id
	bit<8> drop
}

struct psa_egress_deparser_input_metadata_t {
	bit<32> egress_port
}

struct EMPTY_M {
	bit<32> psa_ingress_input_metadata_ingress_port
	bit<8> psa_ingress_output_metadata_drop
	bit<32> psa_ingress_output_metadata_egress_port
	bit<32> local_metadata_depth
	bit<16> local_metadata_ret
	bit<16> Ingress_tmp
	bit<16> Ingress_tmp_0
	bit<32> Ingress_tmp_1
	bit<16> Ingress_tmp_2
	bit<16> Ingress_tmp_3
	bit<32> Ingress_tmp_4
	bit<16> Ingress_tmp_5
}
metadata instanceof EMPTY_M

header ethernet instanceof ethernet_t
header vlan_tag_0 instanceof vlan_tag_h
header vlan_tag_1 instanceof vlan_tag_h


action NoAction args none {
	return
}

table tbl {
	key {
		h.ethernet.srcAddr exact
	}
	actions {
		NoAction
	}
	default_action NoAction args none 
	size 0x10000
}


apply {
	rx m.psa_ingress_input_metadata_ingress_port
	mov m.psa_ingress_output_metadata_drop 0x0
	mov m.local_metadata_depth 0x1
	extract h.ethernet
	jmpeq MYIP_PARSE_VLAN_TAG h.ethernet.etherType 0x8100
	jmp MYIP_ACCEPT
	MYIP_PARSE_VLAN_TAG :	extract h.vlan_tag_0
	add m.local_metadata_depth 0x3
	jmpeq MYIP_PARSE_VLAN_TAG1 h.vlan_tag_0.ether_type 0x8100
	jmp MYIP_ACCEPT
	MYIP_PARSE_VLAN_TAG1 :	extract h.vlan_tag_1
	add m.local_metadata_depth 0x3
	jmpeq MYIP_PARSE_VLAN_TAG2 h.vlan_tag_1.ether_type 0x8100
	jmp MYIP_ACCEPT
	MYIP_PARSE_VLAN_TAG2 :	mov m.psa_ingress_input_metadata_parser_error 0x3
	MYIP_ACCEPT :	jmpneq LABEL_END_0 m.local_metadata_depth 0x0
	mov m.Ingress_tmp h.vlan_tag_0.pcp_cfi_vid
	LABEL_END_0 :	jmpneq LABEL_END_1 m.local_metadata_depth 0x1
	mov m.Ingress_tmp h.vlan_tag_1.pcp_cfi_vid
	LABEL_END_1 :	mov m.Ingress_tmp_0 m.Ingress_tmp
	shr m.Ingress_tmp_0 0x4
	mov m.Ingress_tmp_1 m.Ingress_tmp_0
	mov m.local_metadata_ret m.Ingress_tmp_1
	jmpnv LABEL_FALSE_1 h.ethernet
	jmp LABEL_END_2
	LABEL_FALSE_1 :	jmpneq LABEL_END_3 m.local_metadata_depth 0x0
	mov m.Ingress_tmp_2 h.vlan_tag_0.pcp_cfi_vid
	LABEL_END_3 :	jmpneq LABEL_END_4 m.local_metadata_depth 0x1
	mov m.Ingress_tmp_2 h.vlan_tag_1.pcp_cfi_vid
	LABEL_END_4 :	mov m.Ingress_tmp_3 m.Ingress_tmp_2
	shr m.Ingress_tmp_3 0x4
	mov m.Ingress_tmp_4 m.Ingress_tmp_3
	mov m.Ingress_tmp_5 m.Ingress_tmp_4
	mov m.local_metadata_ret m.Ingress_tmp_5
	add m.local_metadata_ret 0x5
	table tbl
	LABEL_END_2 :	jmpneq LABEL_DROP m.psa_ingress_output_metadata_drop 0x0
	emit h.ethernet
	emit h.vlan_tag_0
	emit h.vlan_tag_1
	tx m.psa_ingress_output_metadata_egress_port
	LABEL_DROP :	drop
}



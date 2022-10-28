#include <core.p4>
#include <pna.p4>

header ethernet_t {
    bit<48> dstAddr;
    bit<48> srcAddr;
    bit<16> etherType;
}

header ipv4_t {
    bit<4>  version;
    bit<4>  ihl;
    bit<8>  diffserv;
    bit<16> totalLen;
    bit<16> identification;
    bit<3>  flags;
    bit<13> fragOffset;
    bit<8>  ttl;
    bit<8>  protocol;
    bit<16> hdrChecksum;
    bit<32> srcAddr;
    bit<32> dstAddr;
}

struct empty_metadata_t {
}

struct main_metadata_t {
    bit<32> port_out;
    bit<32> port_out1;
}

struct headers_t {
    ethernet_t ethernet;
    ipv4_t     ipv4;
}

control PreControlImpl(in headers_t hdr, inout main_metadata_t meta, in pna_pre_input_metadata_t istd, inout pna_pre_output_metadata_t ostd) {
    apply {
    }
}

parser MainParserImpl(packet_in pkt, out headers_t hdr, inout main_metadata_t main_meta, in pna_main_parser_input_metadata_t istd) {
    state start {
        pkt.extract<ethernet_t>(hdr.ethernet);
        transition select(hdr.ethernet.etherType) {
            16w0x800: parse_ipv4;
            default: accept;
        }
    }
    state parse_ipv4 {
        pkt.extract<ipv4_t>(hdr.ipv4);
        transition accept;
    }
}

control MainControlImpl(inout headers_t hdr, inout main_metadata_t user_meta, in pna_main_input_metadata_t istd, inout pna_main_output_metadata_t ostd) {
    @name("MainControlImpl.color_out") PNA_MeterColor_t color_out_0;
    @name("MainControlImpl.out1") PNA_MeterColor_t out1_0;
    @name("MainControlImpl.color_in") PNA_MeterColor_t color_in_0;
    @name("MainControlImpl.tmp_2") bit<32> tmp_2;
    @name("MainControlImpl.meter0") DirectMeter(PNA_MeterType_t.BYTES) meter0_0;
    @name("MainControlImpl.meter1") DirectMeter(PNA_MeterType_t.BYTES) meter1_0;
    @name("MainControlImpl.next_hop") action next_hop(@name("oport") bit<32> oport) {
        out1_0 = meter0_0.execute(color_in_0, 32w1024);
        user_meta.port_out = (out1_0 == PNA_MeterColor_t.GREEN ? 32w1 : 32w0);
        color_out_0 = meter1_0.execute(color_in_0, 32w1024);
        user_meta.port_out1 = (color_out_0 == PNA_MeterColor_t.GREEN ? 32w1 : 32w0);
        send_to_port(oport);
    }
    @name("MainControlImpl.next_hop") action next_hop_1(@name("oport") bit<32> oport_1) {
        out1_0 = meter0_0.execute(color_in_0, 32w1024);
        color_out_0 = meter1_0.execute(color_in_0, 32w1024);
        user_meta.port_out1 = (color_out_0 == PNA_MeterColor_t.GREEN ? 32w1 : 32w0);
        send_to_port(oport_1);
    }
    @name("MainControlImpl.default_route_drop") action default_route_drop() {
        out1_0 = meter0_0.execute(color_in_0, 32w1024);
        user_meta.port_out = (out1_0 == PNA_MeterColor_t.GREEN ? 32w1 : 32w0);
        drop_packet();
    }
    @name("MainControlImpl.default_route_drop") action default_route_drop_1() {
        out1_0 = meter0_0.execute(color_in_0, 32w1024);
        drop_packet();
    }
    @name("MainControlImpl.ipv4_da") table ipv4_da_0 {
        key = {
            hdr.ipv4.dstAddr: exact @name("hdr.ipv4.dstAddr");
        }
        actions = {
            next_hop();
            default_route_drop();
        }
        default_action = default_route_drop();
        pna_direct_meter = meter0_0;
    }
    @name("MainControlImpl.ipv4_da_lpm") table ipv4_da_lpm_0 {
        key = {
            hdr.ipv4.dstAddr: lpm @name("hdr.ipv4.dstAddr");
        }
        actions = {
            next_hop_1();
            default_route_drop_1();
        }
        default_action = default_route_drop_1();
        pna_direct_meter = meter1_0;
    }
    @hidden action pnadpdkdirectmetererr1l129() {
        tmp_2 = 32w1;
    }
    @hidden action pnadpdkdirectmetererr1l129_0() {
        tmp_2 = 32w0;
    }
    @hidden action pnadpdkdirectmetererr1l128() {
        color_out_0 = meter1_0.execute(PNA_MeterColor_t.RED, 32w1024);
    }
    @hidden action pnadpdkdirectmetererr1l129_1() {
        user_meta.port_out1 = tmp_2;
    }
    @hidden action pnadpdkdirectmetererr1l87() {
        color_in_0 = PNA_MeterColor_t.RED;
    }
    @hidden table tbl_pnadpdkdirectmetererr1l87 {
        actions = {
            pnadpdkdirectmetererr1l87();
        }
        const default_action = pnadpdkdirectmetererr1l87();
    }
    @hidden table tbl_pnadpdkdirectmetererr1l128 {
        actions = {
            pnadpdkdirectmetererr1l128();
        }
        const default_action = pnadpdkdirectmetererr1l128();
    }
    @hidden table tbl_pnadpdkdirectmetererr1l129 {
        actions = {
            pnadpdkdirectmetererr1l129();
        }
        const default_action = pnadpdkdirectmetererr1l129();
    }
    @hidden table tbl_pnadpdkdirectmetererr1l129_0 {
        actions = {
            pnadpdkdirectmetererr1l129_0();
        }
        const default_action = pnadpdkdirectmetererr1l129_0();
    }
    @hidden table tbl_pnadpdkdirectmetererr1l129_1 {
        actions = {
            pnadpdkdirectmetererr1l129_1();
        }
        const default_action = pnadpdkdirectmetererr1l129_1();
    }
    apply {
        tbl_pnadpdkdirectmetererr1l87.apply();
        if (hdr.ipv4.isValid()) {
            tbl_pnadpdkdirectmetererr1l128.apply();
            if (color_out_0 == PNA_MeterColor_t.GREEN) {
                tbl_pnadpdkdirectmetererr1l129.apply();
            } else {
                tbl_pnadpdkdirectmetererr1l129_0.apply();
            }
            tbl_pnadpdkdirectmetererr1l129_1.apply();
            ipv4_da_lpm_0.apply();
            ipv4_da_0.apply();
        }
    }
}

control MainDeparserImpl(packet_out pkt, in headers_t hdr, in main_metadata_t user_meta, in pna_main_output_metadata_t ostd) {
    @hidden action pnadpdkdirectmetererr1l143() {
        pkt.emit<ethernet_t>(hdr.ethernet);
        pkt.emit<ipv4_t>(hdr.ipv4);
    }
    @hidden table tbl_pnadpdkdirectmetererr1l143 {
        actions = {
            pnadpdkdirectmetererr1l143();
        }
        const default_action = pnadpdkdirectmetererr1l143();
    }
    apply {
        tbl_pnadpdkdirectmetererr1l143.apply();
    }
}

PNA_NIC<headers_t, main_metadata_t, headers_t, main_metadata_t>(MainParserImpl(), PreControlImpl(), MainControlImpl(), MainDeparserImpl()) main;

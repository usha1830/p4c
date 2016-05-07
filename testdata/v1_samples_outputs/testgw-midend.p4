#include "/home/mbudiu/barefoot/git/p4c/build/../p4include/core.p4"
#include "/home/mbudiu/barefoot/git/p4c/build/../p4include/v1model.p4"

header data_t {
    bit<32> f1;
    bit<32> f2;
    bit<16> f3;
    bit<16> f4;
    bit<8>  f5;
    bit<8>  f6;
    bit<4>  f7;
    bit<4>  f8;
}

header ethernet_t {
    bit<48> dst_addr;
    bit<48> src_addr;
    bit<16> ethertype;
}

struct metadata {
}

struct headers {
    @name("data") 
    data_t     data;
    @name("ethernet") 
    ethernet_t ethernet;
}

parser ParserImpl(packet_in packet, out headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    @name("data") state data {
        packet.extract(hdr.data);
        transition accept;
    }
    @name("start") state start {
        packet.extract(hdr.ethernet);
        transition data;
    }
}

control egress(inout headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    apply {
    }
}

control ingress(inout headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    action NoAction_0() {
    }
    action NoAction_1() {
    }
    action NoAction_2() {
    }
    @name("route_eth") action route_eth_0(bit<9> egress_spec, bit<48> src_addr) {
        standard_metadata.egress_spec = egress_spec;
        hdr.ethernet.src_addr = src_addr;
    }
    @name("noop") action noop_0() {
    }
    @name("noop") action noop() {
    }
    @name("noop") action noop_1() {
    }
    @name("setf2") action setf2_0(bit<32> val) {
        hdr.data.f2 = val;
    }
    @name("setf1") action setf1_0(bit<32> val) {
        hdr.data.f1 = val;
    }
    @name("routing") table routing_0() {
        actions = {
            route_eth_0;
            noop_0;
            NoAction_0;
        }
        key = {
            hdr.ethernet.dst_addr: lpm;
        }
        default_action = NoAction_0();
    }
    @name("test1") table test1_0() {
        actions = {
            setf2_0;
            noop;
            NoAction_1;
        }
        key = {
            hdr.data.f1: exact;
        }
        default_action = NoAction_0();
    }
    @name("test2") table test2_0() {
        actions = {
            setf1_0;
            noop_1;
            NoAction_2;
        }
        key = {
            hdr.data.f2: exact;
        }
        default_action = NoAction_0();
    }
    apply {
        routing_0.apply();
        if (hdr.data.f5 != hdr.data.f6) 
            test1_0.apply();
        else 
            test2_0.apply();
    }
}

control DeparserImpl(packet_out packet, in headers hdr) {
    apply {
        packet.emit(hdr.ethernet);
        packet.emit(hdr.data);
    }
}

control verifyChecksum(in headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    apply {
    }
}

control computeChecksum(inout headers hdr, inout metadata meta) {
    apply {
    }
}

V1Switch(ParserImpl(), verifyChecksum(), ingress(), egress(), computeChecksum(), DeparserImpl()) main;

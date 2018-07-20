header_type ethernet_t {
    fields {
        dstAddr : 48;
        srcAddr : 48;
        etherType : 16;
    }
}

header_type ipv4_t {
    fields {
        version : 4;
        ihl : 4;
        diffserv : 8;
        totalLen : 16;
        identification : 16;
        flags : 3;
        fragOffset : 13;
        ttl : 8;
        protocol : 8;
        hdrChecksum : 16;
        srcAddr : 32;
        dstAddr: 32;
    }
}
header_type arp_t{
 fields {
        hw_type : 16;
        protocol_type : 16;
        hw_size : 8;
        protocol_size : 8;
        opcode : 16;
        srcMac : 48;
        srcIp : 32;
        dstMac : 48;
        dstIp : 32;
    }
}

parser start {
    return parse_ethernet;
}

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP 0x0806

header ethernet_t ethernet;

parser parse_ethernet {
    extract(ethernet);
    return select(latest.etherType) {
        ETHERTYPE_IPV4 : parse_ipv4;
        ETHERTYPE_ARP : parse_arp;
        default: ingress;
    }
}

header ipv4_t ipv4;
header arp_t arp;

field_list ipv4_checksum_list {
        ipv4.version;
        ipv4.ihl;
        ipv4.diffserv;
        ipv4.totalLen;
        ipv4.identification;
        ipv4.flags;
        ipv4.fragOffset;
        ipv4.ttl;
        ipv4.protocol;
        ipv4.srcAddr;
        ipv4.dstAddr;
}

field_list_calculation ipv4_checksum {
    input {
        ipv4_checksum_list;
    }
    algorithm : csum16;
    output_width : 16;
}

calculated_field ipv4.hdrChecksum  {
    verify ipv4_checksum;
    update ipv4_checksum;
}

parser parse_ipv4 {
    extract(ipv4);
    return ingress;
}
parser parse_arp {
    extract(arp);
    return ingress;
}


action _drop() {
    drop();
}

header_type routing_metadata_t {
    fields {
        nhop_ipv4 : 32;
    }
}

metadata routing_metadata_t routing_metadata;

action set_nhop(nhop_ipv4) {
    modify_field(routing_metadata.nhop_ipv4, nhop_ipv4);
    add_to_field(ipv4.ttl, -1);
}
action set_arp_nhop(nhop_ipv4) {

    modify_field(routing_metadata.nhop_ipv4, nhop_ipv4);
}

table ipv4_nhop {
    reads {
        ipv4.dstAddr : exact;
    }
    actions {
        set_nhop;
        _drop;
    }
    size: 1024;
}
table arp_nhop{
    reads {
        arp.dstIp : exact;
    }
    actions {
        set_arp_nhop;
        _drop;
    }
}

action set_dmac(dmac, port) {
    modify_field(ethernet.dstAddr, dmac);
    modify_field(standard_metadata.egress_spec, port);
}

table forward_table {
    reads {
        routing_metadata.nhop_ipv4 : exact;
    }
    actions {
        set_dmac;
        _drop;
    }
    size: 512;
}

action set_smac(smac) {
    modify_field(ethernet.srcAddr, smac);
}

table send_frame {
    reads {
        standard_metadata.egress_spec: exact;
    }
    actions {
        set_smac;
        _drop;
    }
    size: 256;
}

control ingress {
    if((valid(ipv4) and ipv4.ttl>0) or valid(arp)){
        if(valid(ipv4) and ipv4.ttl > 0) {
            apply(ipv4_nhop);
        }
        else if(valid(arp)){
            apply(arp_nhop);
        }
        apply(forward_table);
        apply(send_frame);
    }
}

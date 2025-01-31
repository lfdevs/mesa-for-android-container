#!/usr/bin/env python3

import ctypes
import sys
import json

def get_header_properties(header):
    return {
        'rootNodeOffset': header.rootNodeOffset,
        'aabb': {
            'min_x': header.aabb.min_x,
            'min_y': header.aabb.min_y,
            'min_z': header.aabb.min_z,
            'max_x': header.aabb.max_x,
            'max_y': header.aabb.max_y,
            'max_z': header.aabb.max_z,
        },
        'instance_flags': header.instance_flags,
        'copy_dispatch_size': list(header.copy_dispatch_size),
        'compacted_size': header.compacted_size,
        'serialization_size': header.serialization_size,
        'size': header.size,
        'instance_count': header.instance_count,
        'self_ptr': header.self_ptr,
        'padding': f"{len(header.padding)} uint32_t paddings",
    }

def get_aabb_leaf_properties(node):
    return {
        'leaf_desc': {
            'shaderIndex': node.leaf_desc.shader_index_and_geom_mask & 0xFFFFFF,
            'geomMask': (node.leaf_desc.shader_index_and_geom_mask >> 24) & 0xFF,
            'geomIndex': node.leaf_desc.geometry_id_and_flags & 0xFFFFFF,
            'subType': (node.leaf_desc.geometry_id_and_flags >> 24) & 0xF,
            'reserved0': (node.leaf_desc.geometry_id_and_flags >> 28) & 0x1,
            'DisableOpacityCull': (node.leaf_desc.geometry_id_and_flags >> 29) & 0x1,
            'OpaqueGeometry': (node.leaf_desc.geometry_id_and_flags >> 30) & 0x1,
            'IgnoreRayMultiplier': (node.leaf_desc.geometry_id_and_flags >> 31) & 0x1
        },
        'DW1': node.DW1,
        'primIndex': f"{len(node.primIndex)} uint32"
    }

def get_quad_leaf_properties(node):
    return {
        'leaf_desc': {
            'shaderIndex': node.leaf_desc.shader_index_and_geom_mask & 0xFFFFFF,
            'geomMask': (node.leaf_desc.shader_index_and_geom_mask >> 24) & 0xFF,
            'geomIndex': node.leaf_desc.geometry_id_and_flags & 0xFFFFFF,
            'subType': (node.leaf_desc.geometry_id_and_flags >> 24) & 0xF,
            'reserved0': (node.leaf_desc.geometry_id_and_flags >> 28) & 0x1,
            'DisableOpacityCull': (node.leaf_desc.geometry_id_and_flags >> 29) & 0x1,
            'OpaqueGeometry': (node.leaf_desc.geometry_id_and_flags >> 30) & 0x1,
            'IgnoreRayMultiplier': (node.leaf_desc.geometry_id_and_flags >> 31) & 0x1
        },
        'prim_index0': node.prim_index0,
        'prim_index1_and_flags':{
            'primIndex1Delta': node.prim_index1_and_flags & 0xFFFF,
            'j0': (node.prim_index1_and_flags >> 16) & 0x3,
            'j1': (node.prim_index1_and_flags >> 18) & 0x3,
            'j2': (node.prim_index1_and_flags >> 20) & 0x3,
            'last': (node.prim_index1_and_flags >> 22) & 0x1,
            'pad': (node.prim_index1_and_flags >> 23) & 0x1FF
        },
        'v': [[node.v[i][j] for j in range(3)] for i in range(4)]
    }

def get_internal_node_properties(node):
    # Calculate the actual coordinates, just for visualizing and debugging
    actual_coords = []
    for i in range(6):
        # Turns out the formula is like: x = lower.x + pow(2,exp_x) * 0.xi
        xi_lower = node.lower_x[i] / 256.0  # Convert mantissa to fractional value
        xi_upper = node.upper_x[i] / 256.0  # Convert mantissa to fractional value
        yi_lower = node.lower_y[i] / 256.0  # Convert mantissa to fractional value
        yi_upper = node.upper_y[i] / 256.0  # Convert mantissa to fractional value
        zi_lower = node.lower_z[i] / 256.0  # Convert mantissa to fractional value
        zi_upper = node.upper_z[i] / 256.0  # Convert mantissa to fractional value

        x_lower = node.lower[0] + (2 ** node.exp_x) * xi_lower
        x_upper = node.lower[0] + (2 ** node.exp_x) * xi_upper
        y_lower = node.lower[1] + (2 ** node.exp_y) * yi_lower
        y_upper = node.lower[1] + (2 ** node.exp_y) * yi_upper
        z_lower = node.lower[2] + (2 ** node.exp_z) * zi_lower
        z_upper = node.lower[2] + (2 ** node.exp_z) * zi_upper

        actual_coords.append({
            'x_lower': x_lower,
            'x_upper': x_upper,
            'y_lower': y_lower,
            'y_upper': y_upper,
            'z_lower': z_lower,
            'z_upper': z_upper
        })

    return {
        'lower': list(node.lower),
        'child_offset': node.child_offset,
        'node_type': {
            'nodeType': node.node_type & 0xF,
            'subType': (node.node_type >> 4) & 0xF
        },
        'reserved': node.reserved,
        'exp_x': node.exp_x,
        'exp_y': node.exp_y,
        'exp_z': node.exp_z,
        'node_mask': node.node_mask,
        'child_data': [{
            'blockIncr': node.child_data[i].blockIncr_and_startPrim & 0x3,
            'startPrim': (node.child_data[i].blockIncr_and_startPrim >> 2) & 0xf
        } for i in range(6)],
        'lower_x': list(node.lower_x),
        'upper_x': list(node.upper_x),
        'lower_y': list(node.lower_y),
        'upper_y': list(node.upper_y),
        'lower_z': list(node.lower_z),
        'upper_z': list(node.upper_z),
        'actual_coords': actual_coords
    }

def get_instance_leaf_properties(node):
    return {
        'part0': {
            'shaderIndex': node.part0.shader_index_and_geom_mask & 0xFFFFFF,
            'geomMask': (node.part0.shader_index_and_geom_mask >> 24) & 0xFF,
            'instanceContribution': node.part0.instance_contribution_and_geom_flags & 0xFFFFFF,
            'pad0': (node.part0.instance_contribution_and_geom_flags >> 24) & 0x1F,
            'DisableOpacityCull': (node.part0.instance_contribution_and_geom_flags >> 29) & 0x1,
            'OpaqueGeometry': (node.part0.instance_contribution_and_geom_flags >> 30) & 0x1,
            'pad1': (node.part0.instance_contribution_and_geom_flags >> 31) & 0x1,
            'startNodePtr': node.part0.start_node_ptr_and_inst_flags & 0xFFFFFFFFFFFF,
            'instFlags': (node.part0.start_node_ptr_and_inst_flags >> 48) & 0xFF,
            'ComparisonMode': (node.part0.start_node_ptr_and_inst_flags >> 56) & 0x1,
            'ComparisonValue': (node.part0.start_node_ptr_and_inst_flags >> 57) & 0x7F,
            'world2obj_vx': [node.part0.world2obj_vx_x, node.part0.world2obj_vx_y, node.part0.world2obj_vx_z],
            'world2obj_vy': [node.part0.world2obj_vy_x, node.part0.world2obj_vy_y, node.part0.world2obj_vy_z],
            'world2obj_vz': [node.part0.world2obj_vz_x, node.part0.world2obj_vz_y, node.part0.world2obj_vz_z],
            'obj2world_p': [node.part0.obj2world_p_x, node.part0.obj2world_p_y, node.part0.obj2world_p_z]
        },
        'part1': {
            'bvh_ptr': node.part1.bvh_ptr,
            'instance_id': node.part1.instance_id,
            'instance_index': node.part1.instance_index,
            'obj2world_vx': [node.part1.obj2world_vx_x, node.part1.obj2world_vx_y, node.part1.obj2world_vx_z],
            'obj2world_vy': [node.part1.obj2world_vy_x, node.part1.obj2world_vy_y, node.part1.obj2world_vy_z],
            'obj2world_vz': [node.part1.obj2world_vz_x, node.part1.obj2world_vz_y, node.part1.obj2world_vz_z],
            'world2obj_p': [node.part1.world2obj_p_x, node.part1.world2obj_p_y, node.part1.world2obj_p_z]
        }
    }

class NodeType:
    NODE_TYPE_MIXED = 0x0
    NODE_TYPE_INTERNAL = 0x0
    NODE_TYPE_INSTANCE = 0x1
    NODE_TYPE_QUAD128_STOC = 0x2
    NODE_TYPE_PROCEDURAL = 0x3
    NODE_TYPE_QUAD = 0x4
    NODE_TYPE_QUAD128 = 0x5
    NODE_TYPE_MESHLET = 0x6
    NODE_TYPE_INVALID = 0x7

class VkAabb(ctypes.Structure):
    _fields_ = (
        ('min_x', ctypes.c_float),
        ('min_y', ctypes.c_float),
        ('min_z', ctypes.c_float),
        ('max_x', ctypes.c_float),
        ('max_y', ctypes.c_float),
        ('max_z', ctypes.c_float),
    )

class AnvAccelStructHeader(ctypes.Structure):
    _fields_ = (
        ('rootNodeOffset', ctypes.c_uint64),
        ('aabb', VkAabb),
        ('instance_flags', ctypes.c_uint32),
        ('copy_dispatch_size', ctypes.c_uint32 * 3),
        ('compacted_size', ctypes.c_uint64),
        ('serialization_size', ctypes.c_uint64),
        ('size', ctypes.c_uint64),
        ('instance_count', ctypes.c_uint64),
        ('self_ptr', ctypes.c_uint64),
        ('padding', ctypes.c_uint32 * 42),
    )

class ChildData(ctypes.Structure):
    _fields_ = (
        ('blockIncr_and_startPrim', ctypes.c_uint8),  # Assuming child_data has startPrim field
    )

class AnvInternalNode(ctypes.Structure):
    _fields_ = (
        ('lower', ctypes.c_float * 3),
        ('child_offset', ctypes.c_uint32),
        ('node_type', ctypes.c_uint8),
        ('reserved', ctypes.c_uint8),
        ('exp_x', ctypes.c_int8),
        ('exp_y', ctypes.c_int8),
        ('exp_z', ctypes.c_int8),
        ('node_mask', ctypes.c_uint8),
        ('child_data', ChildData * 6),
        ('lower_x', ctypes.c_uint8 * 6),
        ('upper_x', ctypes.c_uint8 * 6),
        ('lower_y', ctypes.c_uint8 * 6),
        ('upper_y', ctypes.c_uint8 * 6),
        ('lower_z', ctypes.c_uint8 * 6),
        ('upper_z', ctypes.c_uint8 * 6),
    )

class AnvPrimLeafDesc(ctypes.Structure):
    _fields_ = (
        ('shader_index_and_geom_mask', ctypes.c_uint32),
        ('geometry_id_and_flags', ctypes.c_uint32),
    )

class AnvQuadLeafNode(ctypes.Structure):
    _fields_ = (
        ('leaf_desc', AnvPrimLeafDesc),
        ('prim_index0', ctypes.c_uint32),
        ('prim_index1_and_flags', ctypes.c_uint32),
        ('v', (ctypes.c_float * 3) * 4),
    )

class AnvProceduralLeafNode(ctypes.Structure):
    _fields_ = (
        ('leaf_desc', AnvPrimLeafDesc),
        ('DW1', ctypes.c_uint32),
        ('primIndex', ctypes.c_uint32 * 13),
    )

class InstanceLeafPart0(ctypes.Structure):
    _fields_ = (
        ('shader_index_and_geom_mask', ctypes.c_uint32),
        ('instance_contribution_and_geom_flags', ctypes.c_uint32),
        ('start_node_ptr_and_inst_flags', ctypes.c_uint64),
        ('world2obj_vx_x', ctypes.c_float),
        ('world2obj_vx_y', ctypes.c_float),
        ('world2obj_vx_z', ctypes.c_float),
        ('world2obj_vy_x', ctypes.c_float),
        ('world2obj_vy_y', ctypes.c_float),
        ('world2obj_vy_z', ctypes.c_float),
        ('world2obj_vz_x', ctypes.c_float),
        ('world2obj_vz_y', ctypes.c_float),
        ('world2obj_vz_z', ctypes.c_float),
        ('obj2world_p_x', ctypes.c_float),
        ('obj2world_p_y', ctypes.c_float),
        ('obj2world_p_z', ctypes.c_float),
    )

class InstanceLeafPart1(ctypes.Structure):
    _fields_ = (
        ('bvh_ptr', ctypes.c_uint64),
        ('instance_id', ctypes.c_uint32),
        ('instance_index', ctypes.c_uint32),
        ('obj2world_vx_x', ctypes.c_float),
        ('obj2world_vx_y', ctypes.c_float),
        ('obj2world_vx_z', ctypes.c_float),
        ('obj2world_vy_x', ctypes.c_float),
        ('obj2world_vy_y', ctypes.c_float),
        ('obj2world_vy_z', ctypes.c_float),
        ('obj2world_vz_x', ctypes.c_float),
        ('obj2world_vz_y', ctypes.c_float),
        ('obj2world_vz_z', ctypes.c_float),
        ('world2obj_p_x', ctypes.c_float),
        ('world2obj_p_y', ctypes.c_float),
        ('world2obj_p_z', ctypes.c_float),
    )

class AnvInstanceLeaf(ctypes.Structure):
    _fields_ = (
        ('part0', InstanceLeafPart0),
        ('part1', InstanceLeafPart1),
    )

class BVHInterpreter:
    def __init__(self, data):
        self.data = data
        self.nodes = []
        self.relationships = {}
        self.node_counter = 0

    def interpret_structure(self, offset, structure):
        size = ctypes.sizeof(structure)
        if(offset + size > len(self.data)):
            raise ValueError("Not enought data to interpret this structure.")
        buffer = self.data[offset:offset + size]
        return structure.from_buffer_copy(buffer)

    def parse_bvh(self):
        offset = 0
        # Interpret the header
        header = self.interpret_structure(offset, AnvAccelStructHeader)
        offset += header.rootNodeOffset

        # Interpret the rootNode
        self.dfs_interpret_node(offset, AnvInternalNode)

        output = {
            'header': get_header_properties(header),
            'nodes': self.nodes,
            'relationships': self.relationships
        }
        return output

    def determine_child_structure(self, child_node_type):
        if child_node_type == NodeType.NODE_TYPE_MIXED:
            return AnvInternalNode
        elif child_node_type == NodeType.NODE_TYPE_INSTANCE:
            return AnvInstanceLeaf
        elif child_node_type == NodeType.NODE_TYPE_QUAD:
            return AnvQuadLeafNode
        elif child_node_type == NodeType.NODE_TYPE_PROCEDURAL:
            return AnvProceduralLeafNode
        else:
            raise ValueError(f"Unknown node type: {child_node_type}")

    def dfs_interpret_node(self, offset, structure):
        node = self.interpret_structure(offset, structure)
        node_id = self.node_counter;
        self.node_counter += 1

        if structure == AnvInternalNode:
            node_type_str = "AnvInternalNode"
            node_properties = get_internal_node_properties(node)
        elif structure == AnvInstanceLeaf:
            node_type_str = "AnvInstanceLeaf"
            node_properties = get_instance_leaf_properties(node)
        elif structure == AnvQuadLeafNode:
            node_type_str = "AnvQuadLeafNode"
            node_properties = get_quad_leaf_properties(node)
        elif structure == AnvProceduralLeafNode:
            node_type_str = "AnvProceduralLeafNode"
            node_properties = get_aabb_leaf_properties(node)
        else:
            raise ValueError(f"Unknown structure type: {structure}")

        self.nodes.append({
            'id': node_id,
            'type': node_type_str,
            'properties': node_properties
        })

        self.relationships[node_id] = []

        if node_type_str == "AnvInternalNode":
            # DFS its children
            children_offset_start = offset + node.child_offset * 64 # this node's position + child_offset
            isFatLeaf = True if node.node_type != NodeType.NODE_TYPE_MIXED else False
            added_blocks = 0
            for i in range(6):
                blockIncr = node.child_data[i].blockIncr_and_startPrim & 0x3
                child_is_valid = not (node.lower_x[i] & 0x80) or (node.upper_x[i] & 0x80)
                if(not child_is_valid):
                    continue

                # now determine the children's type
                child_node_type = node.node_type if isFatLeaf else ((node.child_data[i].blockIncr_and_startPrim >> 2) & 0xf)

                # find where my child is
                child_offset = children_offset_start + 64 * added_blocks
                added_blocks += blockIncr

                child_node_id = self.dfs_interpret_node(child_offset, self.determine_child_structure(child_node_type))
                self.relationships[node_id].append(child_node_id)

        return node_id


def main():
    with open(sys.argv[1], 'rb') as file1:
        data = file1.read()
    interpreter = BVHInterpreter(data)
    json_output = interpreter.parse_bvh()
    with open("bvh_dump.json", 'w') as f:
        json.dump(json_output, f, indent=4)


if __name__=="__main__":
    main()

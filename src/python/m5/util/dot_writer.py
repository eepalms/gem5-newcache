# Copyright (c) 2012 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Andreas Hansson
#          Uri Wiener

#####################################################################
#
# System visualization using DOT
#
# While config.ini and config.json provide an almost complete listing
# of a system's components and connectivity, they lack a birds-eye view.
# The output generated by do_dot() is a DOT-based figure (pdf) and its
# source dot code. Nodes are components, and edges represent
# the memory hierarchy: the edges are directed, from a master to a slave.
# Initially all nodes are generated, and then all edges are added.
# do_dot should be called with the top-most SimObject (namely root
# but not necessarily), the output folder and the output dot source
# filename. From the given node, both processes (node and edge creation)
# is performed recursivly, traversing all children of the given root.
#
# pydot is required. When missing, no output will be generated.
#
#####################################################################

import m5, os, re
from m5.SimObject import isRoot, isSimObjectVector
from m5.util import warn
try:
    import pydot
except:
    pydot = False

# need to create all nodes (components) before creating edges (memory channels)
def dot_create_nodes(simNode, callgraph):
    if isRoot(simNode):
        label = "root"
    else:
        label = simNode._name
    full_path = re.sub('\.', '_', simNode.path())

    # each component is a sub-graph (cluster)
    cluster = dot_create_cluster(simNode, full_path, label)

    # create nodes per port
    for port_name in simNode._ports.keys():
        port = simNode._port_refs.get(port_name, None)
        if port != None:
            full_port_name = full_path + "_" + port_name
            port_node = dot_create_node(simNode, full_port_name, port_name)
            cluster.add_node(port_node)

    # recurse to children
    if simNode._children:
        for c in simNode._children:
            child = simNode._children[c]
            if isSimObjectVector(child):
                for obj in child:
                    dot_create_nodes(obj, cluster)
            else:
                dot_create_nodes(child, cluster)

    callgraph.add_subgraph(cluster)

# create all edges according to memory hierarchy
def dot_create_edges(simNode, callgraph):
    for port_name in simNode._ports.keys():
        port = simNode._port_refs.get(port_name, None)
        if port != None:
            full_path = re.sub('\.', '_', simNode.path())
            full_port_name = full_path + "_" + port_name
            port_node = dot_create_node(simNode, full_port_name, port_name)
            # create edges
            if type(port) is m5.params.PortRef:
                dot_add_edge(simNode, callgraph, full_port_name, port)
            else:
                for p in port.elements:
                    dot_add_edge(simNode, callgraph, full_port_name, p)

    # recurse to children
    if simNode._children:
        for c in simNode._children:
            child = simNode._children[c]
            if isSimObjectVector(child):
                for obj in child:
                    dot_create_edges(obj, callgraph)
            else:
                dot_create_edges(child, callgraph)

def dot_add_edge(simNode, callgraph, full_port_name, peerPort):
    if peerPort.role == "MASTER":
        peer_port_name = re.sub('\.', '_', peerPort.peer.simobj.path() \
                + "." + peerPort.peer.name)
        callgraph.add_edge(pydot.Edge(full_port_name, peer_port_name))

def dot_create_cluster(simNode, full_path, label):
    # if you read this, feel free to modify colors / style
    return pydot.Cluster( \
                         full_path, \
                         shape = "Mrecord", \
                         label = label, \
                         style = "\"rounded, filled\"", \
                         color = "#000000", \
                         fillcolor = dot_gen_color(simNode), \
                         fontname = "Arial", \
                         fontsize = "14", \
                         fontcolor = "#000000" \
                         )

def dot_create_node(simNode, full_path, label):
    # if you read this, feel free to modify colors / style.
    # leafs may have a different style => seperate function
    return pydot.Node( \
                         full_path, \
                         shape = "Mrecord", \
                         label = label, \
                         style = "\"rounded, filled\"", \
                         color = "#000000", \
                         fillcolor = "#808080", \
                         fontname = "Arial", \
                         fontsize = "14", \
                         fontcolor = "#000000" \
                         )

# generate color for nodes
# currently a simple grayscale. placeholder for aesthetic programmers.
def dot_gen_color(simNode):
    depth = len(simNode.path().split('.'))
    depth = 256 - depth * 16 * 3
    return dot_rgb_to_html(simNode, depth, depth, depth)

def dot_rgb_to_html(simNode, r, g, b):
    return "#%.2x%.2x%.2x" % (r, g, b)

def do_dot(root, outdir, dotFilename):
    if not pydot:
        return
    callgraph = pydot.Dot(graph_type='digraph')
    dot_create_nodes(root, callgraph)
    dot_create_edges(root, callgraph)
    dot_filename = os.path.join(outdir, dotFilename)
    callgraph.write(dot_filename)
    try:
        # dot crashes if the figure is extremely wide.
        # So avoid terminating simulation unnecessarily
        callgraph.write_pdf(dot_filename + ".pdf")
    except:
        warn("failed to generate pdf output from %s", dot_filename)
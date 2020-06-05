"""
 Copyright (C) 2017-2020 Intel Corporation

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
"""

import logging as log

from mo.front.common.replacement import FrontReplacementSubgraph
from mo.graph.graph import Graph
from extensions.ops.mvn import MVN


class LayerNorm(FrontReplacementSubgraph):
    enabled = True
    graph_condition = [lambda graph: graph.graph['cmd_params'].generate_experimental_IR_V10]

    def pattern(self):
        log.info('Enabled LayerNorm replacement')
        return dict(
            nodes=[
                ('pool0',  dict(op='ReduceMean')),
                ('pool1',  dict(op='ReduceMean')),
                ('pow',   dict(op='Pow')),
                ('div',  dict(op='Div')),
                ('sqrt',  dict(op='Pow')),
                ('add0',  dict(op='Add')),
                ('sub',  dict(op='Sub')),
            ],
            edges=[
                ('pool0', 'sub'),
                ('sub', 'pow'),
                ('pow', 'pool1'),
                ('pool1', 'add0'),
                ('add0', 'sqrt'),
                ('sqrt', 'div'),
                ('sub', 'div'),
            ])

    def replace_sub_graph(self, graph: Graph, match: dict):
        inp = match['pool0']
        inp_port = inp.in_port(0).get_source()
        log.error('Found LayerNorm pattern after {} with name {}'.format(inp_port.node.op, inp_port.node.name))
        mvn = MVN(graph, {'name': inp.name + '/MVN_',
                          'eps': 1e-6, 'across_channels': 0, 'normalize_variance': 1}).create_node()
        inp_port.connect(mvn.in_port(0))
        match['div'].out_port(0).get_connection().set_source(mvn.out_port(0))
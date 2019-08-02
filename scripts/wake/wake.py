import docutils
from docutils import nodes
import sphinx
from docutils.parsers import rst
from docutils.parsers.rst import directives 
from sphinx.domains import Domain, Index
from sphinx.domains.std import StandardDomain
from sphinx.roles import XRefRole
from sphinx.directives import ObjectDescription
from sphinx.util.nodes import make_refnode
from sphinx import addnodes

class WakeNode(ObjectDescription):
    """A custom node that describes a Wake function."""

    required_arguments = 1

    option_spec = {
        'params': directives.unchanged_required
    }

    def handle_signature(self, sig, signode):
        print(sig)
        signode += addnodes.desc_name(text=sig)
        return sig

    def add_target_and_index(self, name_cls, sig, signode):
        signode['ids'].append('function-' + sig)
        if 'noindex' not in self.options:
            name = u"{}.{}.{}".format('wake', type(self).__name__, sig)
            objs = self.env.domaindata['wake']['objects']
            if objs != None:
                objs.append((name, sig, 'Wake', self.env.docname, 'function-' + sig, 0))
            else:
                self.env.domaindata['wake']['objects'] = [(name, sig, 'Wake', self.env.docname, 'function-' + sig, 0)]

class WakeDomain(Domain):

    name = 'wake'
    label = 'Wake Function'

    roles = {
        'reref': XRefRole()
    }

    directives = {
        'function': WakeNode,
        'data': WakeNode,
        'tuple': WakeNode
    }

    indices = {
    }

    initial_data = {
        'objects': [], # list of objects
        'obj2param': {} # dict of name -> object dicts
    }

    def get_objects(self):
        for obj in self.data['objects']:
            yield obj
            
    def get_full_qualified_name(self, node):
        """Return full qualified name for a node."""
        return "{}.{}.{}".format('wake', type(node).__name__, node.arguments[0])

    def resolve_xref(self, env, fromdocname, builder, typ, target, node, contnode):
        match = [(docname, anchor)
                 for name, sig, typ, docname, anchor, prio
                 in self.get_objects() if target in sig.split(' ')]
        if len(match) > 0:
            todocname = match[0][0]
            targ = match[0][1]
            return make_refnode(builder, fromdocname, todocname, targ, contnode, targ)
        else:
            return None

def setup(app):
    app.add_domain(WakeDomain)
    return {'version': '0.1'}

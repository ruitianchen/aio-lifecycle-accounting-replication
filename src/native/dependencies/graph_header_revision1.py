"""Strict support for known Greator eight/nine-field, separate-tags graphs."""
import struct

def parse_graph_header(raw, file_size=None):
    if file_size is None:file_size=len(raw)
    if len(raw)<4096:raise ValueError('truncated graph header page')
    nr,nc=struct.unpack_from('<II',raw)
    if nr not in (8,9) or nc!=1:raise ValueError('unsupported graph header shape')
    values=struct.unpack_from('<8Q',raw,8)
    n,d,med,record,per,frozen,loc,size=values
    if not(n>0 and d>0 and med<n and record>=d*4+8 and record%4==0 and 0<per==4096//record and frozen==0 and loc==0):
        raise ValueError('invalid graph layout metadata')
    expected=(1+(n+per-1)//per)*4096
    if not(size==file_size==expected):raise ValueError('graph file/layout size mismatch')
    if nr==9 and struct.unpack_from('<Q',raw,72)[0]!=size:
        raise ValueError('unsupported embedded tags or invalid ninth offset')
    return values

"""Loaded by every Python child of tools/regress.py (PYTHONPATH): routes its oracle runs through the content-addressed
cache of tools/ctlib/oracle_cache.py when CT_ORACLE_CACHE is set. Loaded by FILE, so tools/ is never put on sys.path
(tools/dis.py would shadow the standard library's dis)."""
import importlib.util
import os

if os.environ.get('CT_ORACLE_CACHE'):
    _p = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'ctlib', 'oracle_cache.py')
    _spec = importlib.util.spec_from_file_location('_ct_oracle_cache', _p)
    _m = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_m)
    _m.install()

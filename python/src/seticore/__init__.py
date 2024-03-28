import capnp
capnp.remove_import_hook()

from importlib_resources import files as package_files

hit_capnp = capnp.load(str(package_files('seticore.schema').joinpath("hit.capnp")))
stamp_capnp = capnp.load(str(package_files('seticore.schema').joinpath("stamp.capnp")))
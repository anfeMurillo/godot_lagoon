def can_build(env, platform):
    # The bridge only activates inside the editor (see register_types.cpp,
    # which registers the plugin under TOOLS_ENABLED at EDITOR level), but
    # the module itself must compile for every target so editor and export
    # templates keep the same module list.
    return True


def configure(env):
    pass

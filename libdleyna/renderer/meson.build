install_headers(
    files(
        'control-point-renderer.h'
    ),
    subdir: 'dleyna-1.0/libdleyna/renderer'
)

libdleyna_renderer = library(
    'dleyna-renderer-1.0',
    files(
        'async.c',
        'device.c',
        'gasync-task.c',
        'host-service.c',
        'manager.c',
        'server.c',
        'task.c',
        'upnp.c'
    ),
    version: '1.0.3',
    dependencies : [
        glib,
        gio,
        dleyna_core,
        gupnp,
        gupnp_av,
        gupnp_dlna,
        soup,
        math,
        config_h
    ],
    install: true
)

renderer_dep = declare_dependency (
    link_with: libdleyna_renderer,
    include_directories: include_directories('../..')
)

pkg.generate(
    libdleyna_renderer,
    description: 'UPnP & DLNA renderer library',
    subdirs: 'dleyna-1.0',
    version: meson.project_version(),
    requires: ['gupnp-1.2', 'glib-2.0', 'gio-2.0', 'dleyna-core-1.0']
)

meson.override_dependency('dleyna-renderer-1.0', renderer_dep)

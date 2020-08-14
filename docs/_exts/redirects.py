import os

redirects = [
    ('llvmpipe', 'gallium/drivers/llvmpipe'),
    ('postprocess', 'gallium/postprocess')
]

def create_redirect(dst):
    tpl = '<html><head><meta http-equiv="refresh" content="0; url={0}"><script>window.location.replace("{0}")</script></head></html>'
    return tpl.format(dst)

def create_redirects(app, docname):
    if not app.builder.name == 'html':
        return
    for src, dst in redirects:
        path = os.path.join(app.outdir, '{0}.html'.format(src))
        url = '{0}.html'.format(dst)
        with open(path, 'w') as f:
            f.write(create_redirect(url))

def setup(app):
    app.connect('build-finished', create_redirects)

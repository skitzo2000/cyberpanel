"""Simple WSGI applications

Run them like so::

    lsapi-wsgi test app
    lsapi-wsgi test test_app

"""

def app(environ, start_response):
    start_response('200 OK', [('Content-Type', 'text/plain')])
    return ['Hello World!\n']

def test_app(environ, start_response):
    import cgi
    start_response('200 OK', [('Content-Type', 'text/html')])
    yield '<html><head><title>Hello World!</title></head>\n' \
          '<body>\n' \
          '<p>Hello World!</p>\n' \
          '<table border="1">'
    names = environ.keys()
    names.sort()
    for name in names:
        yield '<tr><td>%s</td><td>%s</td></tr>\n' % (
            name, cgi.escape(`environ[name]`))

    form = cgi.FieldStorage(fp=environ['wsgi.input'], environ=environ,
                            keep_blank_values=1)
    if form.list:
        yield '<tr><th colspan="2">Form data</th></tr>'

    for field in form.list:
        yield '<tr><td>%s</td><td>%s</td></tr>\n' % (
            field.name, field.value)

    yield '</table>\n' \
          '</body></html>\n'

# For testing the InputStream implementation (use curl --data-binary)
def hash_app(environ, start_response):
    import sha
    h = sha.sha()
    h.update(environ['wsgi.input'].read())
    start_response('200 OK', [('Content-Type', 'text/plain')])
    return [h.hexdigest() + '\n']

def hash_app2(environ, start_response):
    import sha
    h = sha.sha()
    inf = environ['wsgi.input']
    s = inf.read(4096)
    while s:
        h.update(s)
        s = inf.read(4096)
    start_response('200 OK', [('Content-Type', 'text/plain')])
    return [h.hexdigest() + '\n']

def hash_app3(environ, start_response):
    import sha
    h = sha.sha()
    lines = 0
    for line in environ['wsgi.input']:
        h.update(line)
        lines += 1
    start_response('200 OK', [('Content-Type', 'text/plain')])
    return ['%s\n%s\n' % (h.hexdigest(), lines)]

def hash_app4(environ, start_response):
    import sha
    h = sha.sha()
    lines = 0
    inf = environ['wsgi.input']
    line = inf.readline(100)
    while line:
        h.update(line)
        lines += 1
        line = inf.readline(100)
    start_response('200 OK', [('Content-Type', 'text/plain')])
    return ['%s\n%s\n' % (h.hexdigest(), lines)]

def wrap_app(environ, start_response):
    start_response('200 OK', [('Content-Type', 'text/plain')])
    f = open('wsgi.c')
    if 'wsgi.file_wrapper' in environ:
        return environ['wsgi.file_wrapper'](f)
    else:
        return iter(f.read(4096), '')

# Wrap a file-like object that doesn't support fileno()
def wrap_app2(environ, start_response):
    import StringIO
    start_response('200 OK', [('Content-Type', 'text/plain')])
    f = open('wsgi.c')
    s = f.read()
    f.close()
    f = StringIO.StringIO()
    f.write(s)
    f.seek(0, 0)
    if 'wsgi.file_wrapper' in environ:
        return environ['wsgi.file_wrapper'](f)
    else:
        return iter(f.read(4096), '')

import atexit
def cleanup():
    print "cleanup called"
atexit.register(cleanup)

# Wrap with wsgiref's validator, if available
try:
    from wsgiref.validate import validator
except ImportError:
    pass
else:
    app = validator(app)
    test_app = validator(test_app)
    hash_app = validator(hash_app)
    hash_app2 = validator(hash_app2)
    hash_app3 = validator(hash_app3)
    #hash_app4 = validator(hash_app4)  # readline(size) not part of spec

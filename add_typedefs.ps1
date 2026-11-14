$path = "c:\Users\User\Documents\GitHub\TermVisualization-BGA-\src\main.cpp"
$content = Get-Content $path
$newContent = New-Object System.Collections.Generic.List[string]
$inserted = $false
foreach ($line in $content) {
    $newContent.Add($line)
    if ($line -eq "// OpenGL ES 2.0 function pointers (loaded via SDL_GL_GetProcAddress on Windows).") {
        $newContent.Add("typedef void (APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);")
        $newContent.Add("typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar** string, const GLint* length);")
        $newContent.Add("typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);")
        $newContent.Add("typedef void (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);")
        $newContent.Add("typedef void (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);")
        $newContent.Add("typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);")
        $newContent.Add("typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);")
        $newContent.Add("typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);")
        $newContent.Add("typedef void (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);")
        $newContent.Add("typedef void (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);")
        $newContent.Add("typedef void (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);")
        $newContent.Add("typedef void (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);")
        $newContent.Add("typedef void (APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);")
        $newContent.Add("typedef GLint (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);")
        $newContent.Add("typedef void (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);")
        $newContent.Add("typedef void (APIENTRY *PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);")
        $newContent.Add("typedef void (APIENTRY *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);")
        $newContent.Add("typedef void (APIENTRY *PFNGLUNIFORMMATRIX3FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);")
        $newContent.Add("typedef void (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);")
        $newContent.Add("typedef void (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);")
        $newContent.Add("typedef void (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage);")
        $newContent.Add("typedef void (APIENTRY *PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid* data);")
        $newContent.Add("typedef void (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);")
        $newContent.Add("typedef void (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);")
        $newContent.Add("typedef void (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);")
        $newContent.Add("typedef void (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* pointer);")
        $newContent.Add("typedef void (APIENTRY *PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);")
        $newContent.Add("typedef void (APIENTRY *PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);")
        $newContent.Add("typedef void (APIENTRY *PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint index);")
        $newContent.Add("typedef void (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);")
        $newContent.Add("typedef GLenum (APIENTRY *PFNGLGETERRORPROC)(void);")
        $newContent.Add("typedef const GLubyte* (APIENTRY *PFNGLGETSTRINGPROC)(GLenum name);")
        $newContent.Add("typedef void (APIENTRY *PFNGLENABLEPROC)(GLenum cap);")
        $newContent.Add("typedef void (APIENTRY *PFNGLDEPTHFUNCPROC)(GLenum func);")
        $newContent.Add("typedef void (APIENTRY *PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);")
        $newContent.Add("typedef void (APIENTRY *PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);")
        $newContent.Add("typedef void (APIENTRY *PFNGLCLEARPROC)(GLbitfield mask);")
        $newContent.Add("typedef void (APIENTRY *PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);")
        $inserted = $true
    }
}
if ($inserted) {
    $newContent | Set-Content $path -Encoding UTF8
    "SUCCESS"
} else {
    "ANCHOR NOT FOUND"
}

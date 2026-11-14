Set fso = CreateObject("Scripting.FileSystemObject")
path = "c:\Users\User\Documents\GitHub\TermVisualization-BGA-\src\main.cpp"
text = fso.OpenTextFile(path, 1).ReadAll
anchor = "// OpenGL ES 2.0 function pointers (loaded via SDL_GL_GetProcAddress on Windows)."
types = "typedef void (APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar** string, const GLint* length);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);" & vbCrLf
types = types & "typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);" & vbCrLf
types = types & "typedef GLint (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLUNIFORMMATRIX3FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid* data);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* pointer);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint index);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);" & vbCrLf
types = types & "typedef GLenum (APIENTRY *PFNGLGETERRORPROC)(void);" & vbCrLf
types = types & "typedef const GLubyte* (APIENTRY *PFNGLGETSTRINGPROC)(GLenum name);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLENABLEPROC)(GLenum cap);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLDEPTHFUNCPROC)(GLenum func);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLCLEARPROC)(GLbitfield mask);" & vbCrLf
types = types & "typedef void (APIENTRY *PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);" & vbCrLf

If InStr(text, anchor) > 0 Then
    text = Replace(text, anchor, anchor & vbCrLf & types)
    fso.CreateTextFile(path, True).Write text
    WScript.Echo "OK"
Else
    WScript.Echo "ANCHOR MISSING"
End If

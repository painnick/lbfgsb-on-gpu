#ifndef __IMDEBUGGL_H__
#define __IMDEBUGGL_H__

#include "imdebug.h"
#include <gl/gl.h>
#include <string>

int format2components(GLenum format);
std::string format2string(GLenum format);


//-----------------------------------------------------------------------------
// Function    : imdebugTexImage
// Description : 
//-----------------------------------------------------------------------------
/**
 * @brief Gets a texture level from OpenGL, and displays it using imdebug.
 * 
 * If argstring is non-NULL, then it is prepended to the string " w=%d h=%d %p"
 * and passed as the imdebug format string.  Otherwise, the correct format
 * ("rgb", "lum", etc.) is determined from the @a format argument, and 
 * prepended to the same string and used as the imdebug format string.
 */ 
inline void imdebugTexImage(GLenum target, GLuint texture, GLenum format = GL_RGB, 
                            GLint level = 0, char *argstring = NULL)
{  
  int w, h;
  int prevTexBind;
  glGetIntegerv(target, &prevTexBind);

  glBindTexture(target, texture);
  glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &w);
  glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &h);
  GLubyte *p = new GLubyte[w * h * format2components(format)];
  
  glGetTexImage(target, level, format, GL_UNSIGNED_BYTE, p);
 
  std::string s = (NULL == argstring) ? format2string(format) : argstring;
  imdebug(s.append(" w=%d h=%d %p").c_str(), w, h, p);
  
  delete [] p;

  glBindTexture(target, prevTexBind);
}

//-----------------------------------------------------------------------------
// Function    : imdebugTexImagef
// Description : 
//-----------------------------------------------------------------------------
/**
 * @brief Gets a texture level from OpenGL, and displays it as floats using imdebug.
 * 
 * If argstring is non-NULL, then it is prepended to the string " w=%d h=%d %p"
 * and passed as the imdebug format string.  Otherwise, the correct format
 * ("rgb", "lum", etc.) is determined from the @a format argument, and 
 * prepended to the same string and used as the imdebug format string.
 */ 
inline void imdebugTexImagef(GLenum target, GLuint texture, GLenum format = GL_RGB, 
                             GLint level = 0, char *argstring = NULL)
{  
  int w, h;
  int prevTexBind;
  glGetIntegerv(target, &prevTexBind);

  glBindTexture(target, texture);
  glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &w);
  glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &h);
  float *p = new float[w * h * format2components(format)];
  
  glGetTexImage(target, level, format, GL_FLOAT, p);
 
  std::string s = (NULL == argstring) ? format2string(format) : argstring;
  imdebug(s.append(" b=32f w=%d h=%d %p").c_str(), w, h, p);
  
  delete [] p;

  glBindTexture(target, prevTexBind);
}


//-----------------------------------------------------------------------------
// Function    : imdebugPixels
// Description : 
//-----------------------------------------------------------------------------
/**
 * @brief performs a glReadPixels(), and then displays the results in imdebug.
 * 
 * If argstring is non-NULL, then it is prepended to the string " w=%d h=%d %p"
 * and passed as the imdebug format string.  Otherwise, the correct format
 * ("rgb", "lum", etc.) is determined from the @a format argument, and 
 * prepended to the same string and used as the imdebug format string.
 */ 
inline void imdebugPixels(GLint x, GLint y, GLsizei width, GLsizei height, 
                          GLenum format = GL_RGB, char *argstring = NULL)
{
  GLubyte *p = new GLubyte[width * height * format2components(format)];
  glReadPixels(x, y, width, height, format, GL_UNSIGNED_BYTE, p);
 
  std::string s = (NULL == argstring) ? format2string(format) : argstring;
  imdebug(s.append(" w=%d h=%d %p").c_str(), width, height, p);

    
  delete [] p;
}

//-----------------------------------------------------------------------------
// Function    : imdebugPixelsf
// Description : 
//-----------------------------------------------------------------------------
/**
 * @brief performs a glReadPixels(), and then displays the results in imdebug.
 * 
 * If argstring is non-NULL, then it is prepended to the string " w=%d h=%d %p"
 * and passed as the imdebug format string.  Otherwise, the correct format
 * ("rgb", "lum", etc.) is determined from the @a format argument, and 
 * prepended to the same string and used as the imdebug format string.
 */ 
inline void imdebugPixelsf(GLint x, GLint y, GLsizei width, GLsizei height, 
                      GLenum format = GL_RGB, char *argstring = NULL)
{
  float *p = new float[width * height * format2components(format)];
  glReadPixels(x, y, width, height, format, GL_FLOAT, p);
 
  std::string s = (NULL == argstring) ? format2string(format) : argstring;
  imdebug(s.append(" b=32f w=%d h=%d %p").c_str(), width, height, p);
    
  delete [] p;
}

//-----------------------------------------------------------------------------
// Function    : imdebugDepth
// Description : 
//-----------------------------------------------------------------------------
/**
 * @brief performs a glReadPixels() on depth, and then displays the results in imdebug.
 * 
 * If argstring is non-NULL, then it is prepended to the string " w=%d h=%d %p"
 * and passed as the imdebug format string.
 */ 
inline void imdebugDepth(GLint x, GLint y, GLsizei width, GLsizei height, 
                         char *argstring = NULL)
{
  GLuint *p = new GLuint[width * height];
  glReadPixels(x, y, width, height, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, p);
 
  std::string s = (NULL == argstring) ? "" : argstring;
  imdebug(s.append("lum b=32 w=%d h=%d %p").c_str(), width, height, p);
    
  delete [] p;
}

//-----------------------------------------------------------------------------
// Function     : imdebugDepthf
// Description	: 
//-----------------------------------------------------------------------------
/**
 * @brief performs a glReadPixels() on depth, and then displays the results in imdebug.
 * 
 * If argstring is non-NULL, then it is prepended to the string 
 * " lum b=32f w=%d h=%d %p"
 * and passed as the imdebug format string.
 */ 
inline void imdebugDepthf(GLint x, GLint y, GLsizei width, GLsizei height, 
                     char *argstring = NULL)
{
  float *p = new float[width * height];
  glReadPixels(x, y, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, p);
 
  std::string s = (NULL == argstring) ? "" : argstring;
  imdebug(s.append(" lum b=32f w=%d h=%d %p").c_str(), width, height, p);
    
  delete [] p;
}

//-----------------------------------------------------------------------------
// Function    : imdebugStencil
// Description : 
//-----------------------------------------------------------------------------
/**
 * @brief performs a glReadPixels() on stencil, and then displays the results in imdebug.
 * 
 * If argstring is non-NULL, then it is prepended to the string 
 *  " lum w=%d h=%d %p"
 * and passed as the imdebug format string.
 */ 
inline void imdebugStencil(GLint x, GLint y, GLsizei width, GLsizei height, 
                           char *argstring = NULL)
{
  GLubyte *p = new GLubyte[width * height];
  glReadPixels(x, y, width, height, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, p);
 
  std::string s = (NULL == argstring) ? "" : argstring;
  imdebug(s.append(" lum w=%d h=%d %p").c_str(), width, height, p);
    
  delete [] p;
}


inline int format2components(GLenum format)
{
  switch(format) 
  {
  case GL_LUMINANCE:
  case GL_INTENSITY:
  case GL_ALPHA:
    return 1;
  	break;
  case GL_LUMINANCE_ALPHA:
    return 2;
  	break;
  case GL_RGB:
  case GL_BGR:
    return 3;
    break;
  case GL_RGBA:
  case GL_BGRA:
  case GL_ABGR_EXT:
    return 4;
    break;
  default:
    return 4;
    break;
  }
}

inline std::string format2string(GLenum format)
{
  switch(format) 
  {
  case GL_LUMINANCE:
  case GL_INTENSITY:
  case GL_ALPHA:
    return "lum";
  	break;
  case GL_LUMINANCE_ALPHA:
    return "luma";
  	break;
  case GL_RGB:
  case GL_BGR:
    return "rgb";
    break;
  case GL_RGBA:
  case GL_BGRA:
  case GL_ABGR_EXT:
    return "rgba";
    break;
  default:
    return "rgb";
    break;
  }
}
#endif //__IMDEBUGGL_H__
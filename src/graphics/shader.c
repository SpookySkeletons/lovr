#include "graphics/shader.h"
#include "graphics/graphics.h"
#include "math/math.h"
#include "resources/shaders.h"
#include <math.h>

static size_t getUniformTypeLength(const Uniform* uniform) {
  size_t size = 0;

  if (uniform->count > 1) {
    size += 2 + floor(log10(uniform->count)) + 1; // "[count]"
  }

  switch (uniform->type) {
    case UNIFORM_MATRIX: size += 4; break;
    case UNIFORM_FLOAT: size += uniform->components == 1 ? 5 : 4; break;
    case UNIFORM_INT: size += uniform->components == 1 ? 3 : 5; break;
    default: break;
  }

  return size;
}

static const char* getUniformTypeName(const Uniform* uniform) {
  switch (uniform->type) {
    case UNIFORM_FLOAT:
      switch (uniform->components) {
        case 1: return "float";
        case 2: return "vec2";
        case 3: return "vec3";
        case 4: return "vec4";
      }
      break;

    case UNIFORM_INT:
      switch (uniform->components) {
        case 1: return "int";
        case 2: return "ivec2";
        case 3: return "ivec3";
        case 4: return "ivec4";
      }
      break;

    case UNIFORM_MATRIX:
      switch (uniform->components) {
        case 2: return "mat2";
        case 3: return "mat3";
        case 4: return "mat4";
      }
      break;

    default: break;
  }

  lovrThrow("Unreachable");
  return "";
}

Shader* lovrShaderInitDefault(Shader* shader, DefaultShader type) {
  switch (type) {
    case SHADER_DEFAULT: return lovrShaderInitGraphics(shader, NULL, NULL);
    case SHADER_CUBE: return lovrShaderInitGraphics(shader, lovrCubeVertexShader, lovrCubeFragmentShader);
    case SHADER_PANO: return lovrShaderInitGraphics(shader, lovrCubeVertexShader, lovrPanoFragmentShader);
    case SHADER_FONT: return lovrShaderInitGraphics(shader, NULL, lovrFontFragmentShader);
    case SHADER_FILL: return lovrShaderInitGraphics(shader, lovrFillVertexShader, NULL);
    default: lovrThrow("Unknown default shader type"); return NULL;
  }
}

ShaderType lovrShaderGetType(Shader* shader) {
  return shader->type;
}

int lovrShaderGetAttributeId(Shader* shader, const char* name) {
  int* id = map_get(&shader->attributes, name);
  return id ? *id : -1;
}

bool lovrShaderHasUniform(Shader* shader, const char* name) {
  return map_get(&shader->uniformMap, name) != NULL;
}

const Uniform* lovrShaderGetUniform(Shader* shader, const char* name) {
  int* index = map_get(&shader->uniformMap, name);
  if (!index) {
    return false;
  }

  return &shader->uniforms.data[*index];
}

static void lovrShaderSetUniform(Shader* shader, const char* name, UniformType type, void* data, int start, int count, int size, const char* debug) {
  int* index = map_get(&shader->uniformMap, name);
  if (!index) {
    return;
  }

  Uniform* uniform = &shader->uniforms.data[*index];
  const char* plural = (uniform->size / size) > 1 ? "s" : "";
  lovrAssert(uniform->type == type, "Unable to send %ss to uniform %s", debug, name);
  lovrAssert((start + count) * size <= uniform->size, "Too many %s%s for uniform %s, maximum is %d", debug, plural, name, uniform->size / size);

  void* dest = uniform->value.bytes + start * size;
  if (!uniform->dirty && !memcmp(dest, data, count * size)) {
    return;
  }

  memcpy(dest, data, count * size);
  uniform->dirty = true;
}

void lovrShaderSetFloats(Shader* shader, const char* name, float* data, int start, int count) {
  lovrShaderSetUniform(shader, name, UNIFORM_FLOAT, data, start, count, sizeof(float), "float");
}

void lovrShaderSetInts(Shader* shader, const char* name, int* data, int start, int count) {
  lovrShaderSetUniform(shader, name, UNIFORM_INT, data, start, count, sizeof(int), "int");
}

void lovrShaderSetMatrices(Shader* shader, const char* name, float* data, int start, int count) {
  lovrShaderSetUniform(shader, name, UNIFORM_MATRIX, data, start, count, sizeof(float), "float");
}

void lovrShaderSetTextures(Shader* shader, const char* name, Texture** data, int start, int count) {
  lovrShaderSetUniform(shader, name, UNIFORM_SAMPLER, data, start, count, sizeof(Texture*), "texture");
}

void lovrShaderSetImages(Shader* shader, const char* name, Image* data, int start, int count) {
  lovrShaderSetUniform(shader, name, UNIFORM_IMAGE, data, start, count, sizeof(Image), "image");
}

void lovrShaderSetColor(Shader* shader, const char* name, Color color) {
  if (lovrGraphicsIsGammaCorrect()) {
    color.r = lovrMathGammaToLinear(color.r);
    color.g = lovrMathGammaToLinear(color.g);
    color.b = lovrMathGammaToLinear(color.b);
  }

  lovrShaderSetUniform(shader, name, UNIFORM_FLOAT, (float*) &color, 0, 4, sizeof(float), "float");
}

void lovrShaderSetBlock(Shader* shader, const char* name, ShaderBlock* source, UniformAccess access) {
  int* id = map_get(&shader->blockMap, name);
  lovrAssert(id, "No shader block named '%s'", name);

  int type = *id & 1;
  int index = *id >> 1;
  UniformBlock* block = &shader->blocks[type].data[index];
  block->access = access;

  if (source != block->source) {
    if (source) {
      lovrAssert(block->uniforms.length == source->uniforms.length, "ShaderBlock must have same number of uniforms as block definition in Shader");
      for (int i = 0; i < block->uniforms.length; i++) {
        const Uniform* u = &block->uniforms.data[i];
        const Uniform* v = &source->uniforms.data[i];
        lovrAssert(u->type == v->type, "Shader is not compatible with ShaderBlock, check type of variable '%s'", v->name);
        lovrAssert(u->offset == v->offset, "Shader is not compatible with ShaderBlock, check order of variable '%s'", v->name);

        // This check is disabled due to observed driver bugs with std140 layouts
        // lovrAssert(u->size == v->size, "Shader is not compatible with ShaderBlock, check count of variable '%s'", v->name);
      }
    }

    lovrRetain(source);
    lovrRelease(block->source);
    block->source = source;
  }
}

// ShaderBlock

BlockType lovrShaderBlockGetType(ShaderBlock* block) {
  return block->type;
}

// TODO use sds!
char* lovrShaderBlockGetShaderCode(ShaderBlock* block, const char* blockName, size_t* length) {

  // Calculate
  size_t size = 0;
  size_t tab = 2;
  size += 15; // "layout(std140) "
  size += block->type == BLOCK_UNIFORM ? 7 : 6; // "uniform" || "buffer"
  size += 1; // " "
  size += strlen(blockName);
  size += 3; // " {\n"
  for (int i = 0; i < block->uniforms.length; i++) {
    size += tab;
    size += getUniformTypeLength(&block->uniforms.data[i]);
    size += 1; // " "
    size += strlen(block->uniforms.data[i].name);
    size += 2; // ";\n"
  }
  size += 3; // "};\n"

  // Allocate
  char* code = malloc(size + 1);

  // Concatenate
  char* s = code;
  s += sprintf(s, "layout(std140) %s %s {\n", block->type == BLOCK_UNIFORM ? "uniform" : "buffer", blockName);
  for (int i = 0; i < block->uniforms.length; i++) {
    const Uniform* uniform = &block->uniforms.data[i];
    if (uniform->count > 1) {
      s += sprintf(s, "  %s %s[%d];\n", getUniformTypeName(uniform), uniform->name, uniform->count);
    } else {
      s += sprintf(s, "  %s %s;\n", getUniformTypeName(uniform), uniform->name);
    }
  }
  s += sprintf(s, "};\n");
  *s = '\0';

  *length = size;
  return code;
}

const Uniform* lovrShaderBlockGetUniform(ShaderBlock* block, const char* name) {
  int* index = map_get(&block->uniformMap, name);
  if (!index) return NULL;

  return &block->uniforms.data[*index];
}

Buffer* lovrShaderBlockGetBuffer(ShaderBlock* block) {
  return block->buffer;
}

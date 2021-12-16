/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "shader.h"

#include <gslogger.h>

namespace {
DEFINE_HILOG_LABEL("AnimationServer::Shader");
} // namespace

Shader::Shader(const std::string& vertexSource, const std::string& fragmentSource)
{
    rendererID_ = CreateShader(vertexSource, fragmentSource);
}

Shader::~Shader()
{
    if (rendererID_ != 0) {
        glDeleteProgram(rendererID_);
    }
}

void Shader::Bind() const
{
    glUseProgram(rendererID_);
}

void Shader::Unbind() const
{
    glUseProgram(0);
}

void Shader::SetUniform1i(const std::string& name, int32_t v)
{
    glUniform1i(GetUniformLocation(name), v);
}

void Shader::SetUniform1f(const std::string& name, float v)
{
    glUniform1f(GetUniformLocation(name), v);
}

void Shader::SetUniform4f(const std::string& name, float v0, float v1, float v2, float v3)
{
    glUniform4f(GetUniformLocation(name), v0, v1, v2, v3);
}

void Shader::SetUniformMat4f(const std::string& name, const Matrix<GLfloat>& matrix)
{
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, matrix);
}

int32_t Shader::GetAttribLocation(const std::string& name)
{
    return glGetAttribLocation(rendererID_, name.c_str());
}

int32_t Shader::GetUniformLocation(const std::string& name)
{
    if (uniformLocationCache_.find(name) != uniformLocationCache_.end()) {
        return uniformLocationCache_[name];
    }
    int32_t location = glGetUniformLocation(rendererID_, name.c_str());
    if (location == -1) {
        GSLOG2HI(WARN) << "Warning: uniform '" << name << "' doesn't exist!" << std::endl;
    }

    uniformLocationCache_[name] = location;
    return location;
}

uint32_t Shader::CompileShader(uint32_t type, const std::string& source)
{
    uint32_t id = glCreateShader(type);
    const char *src = source.c_str();
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    int32_t result;
    glGetShaderiv(id, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
        int32_t length;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
        char* message = static_cast<char*>(alloca(length));
        glGetShaderInfoLog(id, length, &length, message);
        GSLOG2HI(ERROR) << "CompileShader Failed: " << message;
        glDeleteShader(id);
        return 0;
    }

    return id;
}

uint32_t Shader::CreateShader(const std::string& vertexShader, const std::string& fragmentShader)
{
    uint32_t program = glCreateProgram();
    uint32_t vs = CompileShader(GL_VERTEX_SHADER, vertexShader);
    uint32_t fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShader);

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    constexpr int32_t infoLength = 1024;
    GLint success = 1;
    char infoLogs[infoLength] = {};
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, infoLength, nullptr, infoLogs);
        GSLOG2HI(ERROR) << "program error[" << glGetError() << "]: " << infoLogs;
        return 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

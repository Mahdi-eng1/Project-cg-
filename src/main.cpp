#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned int SCR_WIDTH = 1280;
constexpr unsigned int SCR_HEIGHT = 720;
constexpr float LEVEL_TIME_LIMIT = 90.0f;

bool gKeys[1024] = {};
float gLastX = SCR_WIDTH * 0.5f;
float gLastY = SCR_HEIGHT * 0.5f;
bool gFirstMouse = true;
float gDeltaTime = 0.0f;
float gLastFrame = 0.0f;
bool gCaptureMouse = true;
bool gTabLatch = false;
bool gResetLatch = false;
} // namespace

class Shader {
public:
    unsigned int id = 0;

    Shader(const char* vertexSrc, const char* fragmentSrc) {
        const unsigned int v = compile(GL_VERTEX_SHADER, vertexSrc);
        const unsigned int f = compile(GL_FRAGMENT_SHADER, fragmentSrc);
        id = glCreateProgram();
        glAttachShader(id, v);
        glAttachShader(id, f);
        glLinkProgram(id);
        checkProgram(id);
        glDeleteShader(v);
        glDeleteShader(f);
    }

    ~Shader() {
        if (id != 0) {
            glDeleteProgram(id);
        }
    }

    void use() const { glUseProgram(id); }
    void setBool(const std::string& name, bool value) const { glUniform1i(glGetUniformLocation(id, name.c_str()), value ? 1 : 0); }
    void setInt(const std::string& name, int value) const { glUniform1i(glGetUniformLocation(id, name.c_str()), value); }
    void setFloat(const std::string& name, float value) const { glUniform1f(glGetUniformLocation(id, name.c_str()), value); }
    void setVec3(const std::string& name, const glm::vec3& value) const { glUniform3fv(glGetUniformLocation(id, name.c_str()), 1, glm::value_ptr(value)); }
    void setMat4(const std::string& name, const glm::mat4& value) const { glUniformMatrix4fv(glGetUniformLocation(id, name.c_str()), 1, GL_FALSE, glm::value_ptr(value)); }

private:
    static unsigned int compile(unsigned int type, const char* source) {
        const unsigned int shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        checkShader(shader, type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT");
        return shader;
    }

    static void checkShader(unsigned int shader, const char* label) {
        int ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok == GL_TRUE) return;
        char info[1024] = {};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(info)), nullptr, info);
        throw std::runtime_error(std::string("Shader compile error [") + label + "]:\n" + info);
    }

    static void checkProgram(unsigned int program) {
        int ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (ok == GL_TRUE) return;
        char info[1024] = {};
        glGetProgramInfoLog(program, static_cast<GLsizei>(sizeof(info)), nullptr, info);
        throw std::runtime_error(std::string("Program link error:\n") + info);
    }
};

class Camera {
public:
    glm::vec3 position {1.5f, 1.3f, 1.5f};
    glm::vec3 front {0.0f, 0.0f, -1.0f};
    glm::vec3 up {0.0f, 1.0f, 0.0f};
    glm::vec3 right {1.0f, 0.0f, 0.0f};
    glm::vec3 worldUp {0.0f, 1.0f, 0.0f};
    float yaw = -90.0f;
    float pitch = 0.0f;
    float fov = 60.0f;
    float mouseSensitivity = 0.11f;

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    void processMouseMovement(float xOffset, float yOffset) {
        yaw += xOffset * mouseSensitivity;
        pitch += yOffset * mouseSensitivity;
        pitch = std::clamp(pitch, -89.0f, 89.0f);
        updateVectors();
    }

    glm::vec3 forwardXZ() const {
        glm::vec3 f(front.x, 0.0f, front.z);
        if (glm::length(f) < 0.0001f) return glm::vec3(0.0f, 0.0f, -1.0f);
        return glm::normalize(f);
    }

    glm::vec3 rightXZ() const {
        return glm::normalize(glm::cross(forwardXZ(), worldUp));
    }

private:
    void updateVectors() {
        glm::vec3 newFront;
        newFront.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
        newFront.y = std::sin(glm::radians(pitch));
        newFront.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
        front = glm::normalize(newFront);
        right = glm::normalize(glm::cross(front, worldUp));
        up = glm::normalize(glm::cross(right, front));
    }
};

struct AABB {
    glm::vec3 min;
    glm::vec3 max;
};

struct Wall {
    glm::vec3 position {0.0f};
    glm::vec3 scale {1.0f, 2.0f, 1.0f};
    AABB box() const {
        return {position - scale * 0.5f, position + scale * 0.5f};
    }
};

struct Collectible {
    glm::vec3 position {0.0f};
    glm::vec3 scale {0.35f};
    bool collected = false;
};

struct Player {
    glm::vec3 position {1.5f, 0.5f, 1.5f};
    glm::vec3 size {0.6f, 1.0f, 0.6f};
    float moveSpeed = 3.8f;
    AABB box() const { return {position - size * 0.5f, position + size * 0.5f}; }
};

enum class GameState { Playing, Won, Lost };

struct MeshData {
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
};

struct Textures {
    unsigned int ground = 0;
    unsigned int wall = 0;
    unsigned int collectible = 0;
    unsigned int player = 0;
};

struct Game {
    Player player;
    Camera camera;
    std::vector<Wall> walls;
    std::vector<Collectible> collectibles;

    MeshData cube;
    MeshData plane;
    Textures textures;
    Shader* shader = nullptr;

    int score = 0;
    int totalCollectibles = 0;
    float timeRemaining = LEVEL_TIME_LIMIT;
    GameState state = GameState::Playing;
};

Game* gGame = nullptr;

constexpr const char* VERTEX_SRC = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = projection * view * worldPos;
}
)";

constexpr const char* FRAGMENT_SRC = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform sampler2D texture1;
uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 viewPos;
uniform vec3 objectColor;
uniform bool useTexture;
uniform vec3 tintColor;

void main() {
    vec3 ambient = 0.48 * lightColor;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * 1.15 * lightColor;

    vec3 base = objectColor;
    if (useTexture) {
        base = texture(texture1, TexCoord).rgb;
    }
    base *= tintColor;

    vec3 lit = (ambient + diffuse) * base;

    float d = length(viewPos - FragPos);
    float fog = clamp(exp(-0.006 * d * d), 0.0, 1.0);
    vec3 fogColor = vec3(0.22, 0.30, 0.40);

    FragColor = vec4(mix(fogColor, lit, fog), 1.0);
}
)";

bool checkAABB(const AABB& a, const AABB& b) {
    return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
           (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
           (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

bool playerHitsWall(const Player& player, const std::vector<Wall>& walls) {
    const AABB p = player.box();
    for (const Wall& w : walls) {
        if (checkAABB(p, w.box())) return true;
    }
    return false;
}

void movePlayerWithCollision(Game& game, const glm::vec3& delta) {
    Player test = game.player;
    test.position.x += delta.x;
    if (!playerHitsWall(test, game.walls)) game.player.position.x += delta.x;

    test = game.player;
    test.position.z += delta.z;
    if (!playerHitsWall(test, game.walls)) game.player.position.z += delta.z;
}

unsigned int createFallbackCheckerTexture(const glm::u8vec3& c0, const glm::u8vec3& c1) {
    std::array<unsigned char, 4 * 4 * 3> pixels {};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const bool alt = ((x + y) % 2) != 0;
            const glm::u8vec3 c = alt ? c1 : c0;
            const int i = (y * 4 + x) * 3;
            pixels[static_cast<size_t>(i + 0)] = c.r;
            pixels[static_cast<size_t>(i + 1)] = c.g;
            pixels[static_cast<size_t>(i + 2)] = c.b;
        }
    }

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 4, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

unsigned int loadTexture(const char* path, bool alpha = false) {
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    int w = 0;
    int h = 0;
    int c = 0;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &w, &h, &c, 0);
    if (data == nullptr) {
        std::cerr << "Texture load failed: " << path << " | using fallback checker texture\n";
        glDeleteTextures(1, &tex);
        return createFallbackCheckerTexture(glm::u8vec3(180, 60, 180), glm::u8vec3(50, 30, 50));
    }

    GLenum format = GL_RGB;
    if (c == 1) format = GL_RED;
    else if (c == 4) format = GL_RGBA;

    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<int>(format), w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, alpha ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, alpha ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    return tex;
}

unsigned int loadTextureFirstFound(std::initializer_list<const char*> candidates, bool alpha = false) {
    for (const char* candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            std::cout << "Loading texture: " << candidate << '\n';
            return loadTexture(candidate, alpha);
        }
    }

    std::cerr << "None of the texture candidates exist:\n";
    for (const char* candidate : candidates) {
        std::cerr << "  - " << candidate << '\n';
    }
    return createFallbackCheckerTexture(glm::u8vec3(180, 60, 180), glm::u8vec3(50, 30, 50));
}

void createCubeMesh(MeshData& mesh) {
    const float vertices[] = {
        // pos                  // normal              // uv
        -0.5f,-0.5f, 0.5f,      0.0f, 0.0f, 1.0f,      0.0f, 0.0f,
         0.5f,-0.5f, 0.5f,      0.0f, 0.0f, 1.0f,      1.0f, 0.0f,
         0.5f, 0.5f, 0.5f,      0.0f, 0.0f, 1.0f,      1.0f, 1.0f,
        -0.5f, 0.5f, 0.5f,      0.0f, 0.0f, 1.0f,      0.0f, 1.0f,

        -0.5f,-0.5f,-0.5f,      0.0f, 0.0f,-1.0f,      1.0f, 0.0f,
         0.5f,-0.5f,-0.5f,      0.0f, 0.0f,-1.0f,      0.0f, 0.0f,
         0.5f, 0.5f,-0.5f,      0.0f, 0.0f,-1.0f,      0.0f, 1.0f,
        -0.5f, 0.5f,-0.5f,      0.0f, 0.0f,-1.0f,      1.0f, 1.0f,

        -0.5f,-0.5f,-0.5f,     -1.0f, 0.0f, 0.0f,      0.0f, 0.0f,
        -0.5f,-0.5f, 0.5f,     -1.0f, 0.0f, 0.0f,      1.0f, 0.0f,
        -0.5f, 0.5f, 0.5f,     -1.0f, 0.0f, 0.0f,      1.0f, 1.0f,
        -0.5f, 0.5f,-0.5f,     -1.0f, 0.0f, 0.0f,      0.0f, 1.0f,

         0.5f,-0.5f,-0.5f,      1.0f, 0.0f, 0.0f,      1.0f, 0.0f,
         0.5f,-0.5f, 0.5f,      1.0f, 0.0f, 0.0f,      0.0f, 0.0f,
         0.5f, 0.5f, 0.5f,      1.0f, 0.0f, 0.0f,      0.0f, 1.0f,
         0.5f, 0.5f,-0.5f,      1.0f, 0.0f, 0.0f,      1.0f, 1.0f,

        -0.5f, 0.5f,-0.5f,      0.0f, 1.0f, 0.0f,      0.0f, 1.0f,
         0.5f, 0.5f,-0.5f,      0.0f, 1.0f, 0.0f,      1.0f, 1.0f,
         0.5f, 0.5f, 0.5f,      0.0f, 1.0f, 0.0f,      1.0f, 0.0f,
        -0.5f, 0.5f, 0.5f,      0.0f, 1.0f, 0.0f,      0.0f, 0.0f,

        -0.5f,-0.5f,-0.5f,      0.0f,-1.0f, 0.0f,      0.0f, 0.0f,
         0.5f,-0.5f,-0.5f,      0.0f,-1.0f, 0.0f,      1.0f, 0.0f,
         0.5f,-0.5f, 0.5f,      0.0f,-1.0f, 0.0f,      1.0f, 1.0f,
        -0.5f,-0.5f, 0.5f,      0.0f,-1.0f, 0.0f,      0.0f, 1.0f
    };

    const unsigned int indices[] = {
         0, 1, 2, 2, 3, 0,
         4, 5, 6, 6, 7, 4,
         8, 9,10,10,11, 8,
        12,13,14,14,15,12,
        16,17,18,18,19,16,
        20,21,22,22,23,20
    };

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void createPlaneMesh(MeshData& mesh) {
    const float vertices[] = {
        // pos                // normal       // uv
        -0.5f, 0.0f,-0.5f,    0,1,0,          0,0,
         0.5f, 0.0f,-0.5f,    0,1,0,          1,0,
         0.5f, 0.0f, 0.5f,    0,1,0,          1,1,
        -0.5f, 0.0f, 0.5f,    0,1,0,          0,1
    };
    const unsigned int indices[] = {0,1,2,2,3,0};

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void resetGame(Game& game) {
    game.player = Player {};
    game.camera = Camera {};
    game.camera.position = game.player.position + glm::vec3(0.0f, 0.8f, 0.0f);
    game.walls.clear();
    game.collectibles.clear();
    game.score = 0;
    game.timeRemaining = LEVEL_TIME_LIMIT;
    game.state = GameState::Playing;
}

void buildLevel(Game& game) {
    for (int i = 0; i < 20; ++i) {
        game.walls.push_back({glm::vec3(static_cast<float>(i), 1.0f, 0.0f), glm::vec3(1,2,1)});
        game.walls.push_back({glm::vec3(static_cast<float>(i), 1.0f, 19.0f), glm::vec3(1,2,1)});
        game.walls.push_back({glm::vec3(0.0f, 1.0f, static_cast<float>(i)), glm::vec3(1,2,1)});
        game.walls.push_back({glm::vec3(19.0f, 1.0f, static_cast<float>(i)), glm::vec3(1,2,1)});
    }

    game.walls.push_back({glm::vec3(4.0f, 1.0f, 4.0f),   glm::vec3(1.0f, 2.0f, 6.0f)});
    game.walls.push_back({glm::vec3(8.0f, 1.0f, 10.0f),  glm::vec3(6.0f, 2.0f, 1.0f)});
    game.walls.push_back({glm::vec3(12.0f, 1.0f, 6.0f),  glm::vec3(1.0f, 2.0f, 8.0f)});
    game.walls.push_back({glm::vec3(15.0f, 1.0f, 14.0f), glm::vec3(5.0f, 2.0f, 1.0f)});
    game.walls.push_back({glm::vec3(6.0f, 1.0f, 15.0f),  glm::vec3(1.0f, 2.0f, 4.0f)});
    game.walls.push_back({glm::vec3(10.0f, 1.0f, 3.0f),  glm::vec3(4.0f, 2.0f, 1.0f)});

    game.collectibles.push_back({glm::vec3(2.0f, 0.5f, 2.0f)});
    game.collectibles.push_back({glm::vec3(6.0f, 0.5f, 8.0f)});
    game.collectibles.push_back({glm::vec3(10.0f, 0.5f, 15.0f)});
    game.collectibles.push_back({glm::vec3(17.0f, 0.5f, 4.0f)});
    game.collectibles.push_back({glm::vec3(14.0f, 0.5f, 17.0f)});

    game.totalCollectibles = static_cast<int>(game.collectibles.size());
}

void updateGame(Game& game, float dt) {
    if (game.state != GameState::Playing) return;
    game.timeRemaining = std::max(0.0f, game.timeRemaining - dt);
    if (game.timeRemaining <= 0.0f) {
        game.state = GameState::Lost;
        return;
    }

    const AABB playerBox = game.player.box();
    for (Collectible& c : game.collectibles) {
        if (c.collected) continue;
        const AABB itemBox {c.position - c.scale * 0.5f, c.position + c.scale * 0.5f};
        if (checkAABB(playerBox, itemBox)) {
            c.collected = true;
            ++game.score;
        }
    }

    if (game.score >= game.totalCollectibles) {
        game.state = GameState::Won;
    }

    game.camera.position = game.player.position + glm::vec3(0.0f, 0.8f, 0.0f);
}

void processInput(GLFWwindow* window, Game& game) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, 1);
    }

    const bool tabDown = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
    if (tabDown && !gTabLatch) {
        gCaptureMouse = !gCaptureMouse;
        glfwSetInputMode(window, GLFW_CURSOR, gCaptureMouse ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
    gTabLatch = tabDown;

    const bool rDown = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
    if (rDown && !gResetLatch) {
        resetGame(game);
        buildLevel(game);
    }
    gResetLatch = rDown;

    if (game.state != GameState::Playing) return;

    glm::vec3 move(0.0f);
    if (gKeys[GLFW_KEY_W]) move += game.camera.forwardXZ();
    if (gKeys[GLFW_KEY_S]) move -= game.camera.forwardXZ();
    if (gKeys[GLFW_KEY_D]) move += game.camera.rightXZ();
    if (gKeys[GLFW_KEY_A]) move -= game.camera.rightXZ();

    if (glm::length(move) > 0.0001f) {
        move = glm::normalize(move) * game.player.moveSpeed * gDeltaTime;
        movePlayerWithCollision(game, move);
    }
}

void drawCube(const Game& game, unsigned int tex, const glm::vec3& position, const glm::vec3& scale, const glm::vec3& tint) {
    game.shader->use();
    glm::mat4 model(1.0f);
    model = glm::translate(model, position);
    model = glm::scale(model, scale);
    game.shader->setMat4("model", model);
    game.shader->setVec3("objectColor", glm::vec3(1.0f));
    game.shader->setVec3("tintColor", tint);
    game.shader->setBool("useTexture", true);
    game.shader->setInt("texture1", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(game.cube.vao);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
}

void drawGround(const Game& game) {
    game.shader->use();
    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(9.5f, 0.0f, 9.5f));
    model = glm::scale(model, glm::vec3(20.0f, 1.0f, 20.0f));
    game.shader->setMat4("model", model);
    game.shader->setVec3("objectColor", glm::vec3(1.0f));
    game.shader->setVec3("tintColor", glm::vec3(1.0f));
    game.shader->setBool("useTexture", true);
    game.shader->setInt("texture1", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, game.textures.ground);
    glBindVertexArray(game.plane.vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
}

void render(Game& game) {
    glClearColor(0.10f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    game.shader->use();
    const glm::mat4 projection = glm::perspective(glm::radians(game.camera.fov), static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT), 0.1f, 100.0f);
    const glm::mat4 view = game.camera.getViewMatrix();
    game.shader->setMat4("projection", projection);
    game.shader->setMat4("view", view);
    game.shader->setVec3("lightPos", glm::vec3(11.0f, 13.0f, 10.0f));
    game.shader->setVec3("lightColor", glm::vec3(1.0f));
    game.shader->setVec3("viewPos", game.camera.position);

    drawGround(game);

    for (const Wall& w : game.walls) {
        drawCube(game, game.textures.wall, w.position, w.scale, glm::vec3(1.0f));
    }

    const float spin = static_cast<float>(glfwGetTime()) * 1.5f;
    for (const Collectible& c : game.collectibles) {
        if (c.collected) continue;
        glm::vec3 animatedPos = c.position;
        animatedPos.y += std::sin(spin + c.position.x) * 0.07f;
        drawCube(game, game.textures.collectible, animatedPos, c.scale, glm::vec3(1.15f, 1.15f, 1.0f));
    }

    glm::vec3 playerTint(1.0f);
    if (game.state == GameState::Won) playerTint = glm::vec3(0.6f, 1.2f, 0.6f);
    if (game.state == GameState::Lost) playerTint = glm::vec3(1.2f, 0.6f, 0.6f);
    drawCube(game, game.textures.player, game.player.position, game.player.size, playerTint);
}

void updateTitle(GLFWwindow* window, const Game& game) {
    std::string state = "PLAYING";
    if (game.state == GameState::Won) state = "YOU WIN";
    if (game.state == GameState::Lost) state = "YOU LOSE";
    std::ostringstream ss;
    ss << "Maze Collector 3D | Goal: Collect all coins before timer ends"
       << " | Score: " << game.score << "/" << game.totalCollectibles
       << " | Time: " << std::fixed << std::setprecision(1) << game.timeRemaining
       << " | State: " << state
       << " | Controls: [WASD + Mouse], [R reset], [Tab cursor], [ESC quit]";
    if (game.state != GameState::Playing) {
        ss << " | Press R to restart";
    }
    glfwSetWindowTitle(window, ss.str().c_str());
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    (void)window;
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    (void)window;
    if (gGame == nullptr || !gCaptureMouse) return;
    if (gFirstMouse) {
        gLastX = static_cast<float>(xpos);
        gLastY = static_cast<float>(ypos);
        gFirstMouse = false;
        return;
    }

    const float xOffset = static_cast<float>(xpos) - gLastX;
    const float yOffset = gLastY - static_cast<float>(ypos);
    gLastX = static_cast<float>(xpos);
    gLastY = static_cast<float>(ypos);
    gGame->camera.processMouseMovement(xOffset, yOffset);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    (void)mods;
    if (key >= 0 && key < 1024) {
        if (action == GLFW_PRESS) gKeys[key] = true;
        if (action == GLFW_RELEASE) gKeys[key] = false;
    }
}

void updateDeltaTime() {
    const float now = static_cast<float>(glfwGetTime());
    gDeltaTime = now - gLastFrame;
    gLastFrame = now;
}

bool initWindow(GLFWwindow*& window) {
    if (glfwInit() == 0) {
        std::cerr << "GLFW init failed.\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Maze Collector 3D", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "GLFW window creation failed.\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0) {
        std::cerr << "GLAD init failed.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    return true;
}

void initGame(Game& game) {
    game.shader = new Shader(VERTEX_SRC, FRAGMENT_SRC);
    createCubeMesh(game.cube);
    createPlaneMesh(game.plane);

    game.textures.ground = loadTextureFirstFound({
        "assets/textures/grass.jpg",
        "assets/textures/grass.jpeg",
        "assets/textures/grass.jpg.jpeg",
        "assets/textures/ground.ppm"
    });
    game.textures.wall = loadTextureFirstFound({
        "assets/textures/wall.png",
        "assets/textures/wall.jpeg",
        "assets/textures/wall.png.jpeg",
        "assets/textures/wall.ppm"
    }, true);
    game.textures.collectible = loadTextureFirstFound({
        "assets/textures/collectible.png",
        "assets/textures/collectible.jpg",
        "assets/textures/collectible.ppm"
    }, true);
    game.textures.player = loadTextureFirstFound({
        "assets/textures/player.png",
        "assets/textures/player.jpg",
        "assets/textures/player.ppm"
    }, true);

    resetGame(game);
    buildLevel(game);
}

void cleanupGame(Game& game) {
    delete game.shader;
    game.shader = nullptr;

    glDeleteVertexArrays(1, &game.cube.vao);
    glDeleteBuffers(1, &game.cube.vbo);
    glDeleteBuffers(1, &game.cube.ebo);
    glDeleteVertexArrays(1, &game.plane.vao);
    glDeleteBuffers(1, &game.plane.vbo);
    glDeleteBuffers(1, &game.plane.ebo);

    glDeleteTextures(1, &game.textures.ground);
    glDeleteTextures(1, &game.textures.wall);
    glDeleteTextures(1, &game.textures.collectible);
    glDeleteTextures(1, &game.textures.player);
}

int main() {
    GLFWwindow* window = nullptr;
    if (!initWindow(window)) return -1;

    Game game;
    gGame = &game;
    try {
        initGame(game);
    } catch (const std::exception& ex) {
        std::cerr << "Initialization failed: " << ex.what() << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    while (glfwWindowShouldClose(window) == 0) {
        updateDeltaTime();
        processInput(window, game);
        updateGame(game, gDeltaTime);
        updateTitle(window, game);
        render(game);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    cleanupGame(game);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

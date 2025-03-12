#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include "myopengl.hpp"
#include <vector>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace myopengl;

// Variables globales para la cámara y controles
static float Yaw = 0.0f;
static float Pitch = 0.0f;
float mouseSensitivity = 0.5f;
glm::vec3 wasd_Movement = { 0.0f, 0.0f, 0.0f };
float movementSpeed = 5.0f;
float lastFrame = 0.0f;
float deltaTime = 0.0f;

// --- SHADER SOURCES ---
// Shader principal (para iluminación y sombras)
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;
out vec4 FragPosLightSpace;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    // Transformamos la normal (asegurando la corrección en escalados no uniformes)
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    gl_Position = projection * view * worldPos;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 FragPosLightSpace;

uniform sampler2D diffuseTexture; // Usado si no se activa multitextura
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform sampler2D texture3;
uniform bool useMultiTexture;
uniform bool useTexture;
uniform float mixRatio1;
uniform float mixRatio2;
uniform float mixRatio3;

uniform sampler2D shadowMap;
uniform vec3 lightDir; // Dirección de la luz (normalizada)
uniform vec3 viewPos;

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    // Dividir por w y transformar de [-1,1] a [0,1]
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    // Obtener la profundidad más cercana del mapa de sombras
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;
    // Bias para reducir artefactos (shadow acne)
    float bias = max(0.05 * (1.0 - dot(normal, -lightDir)), 0.005);
    // Determinar si está en sombra
    float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
    // Si el fragmento está fuera del rango, no aplicar sombra
    if(projCoords.z > 1.0)
        shadow = 0.0;
    return shadow;
}

void main() {
    vec3 baseColor;
    if(useTexture) {
        if(useMultiTexture) {
            vec4 tex1 = texture(texture1, TexCoord) * mixRatio1;
            vec4 tex2 = texture(texture2, TexCoord) * mixRatio2;
            vec4 tex3 = texture(texture3, TexCoord) * mixRatio3;
            float totalRatio = mixRatio1 + mixRatio2 + mixRatio3;
            if(totalRatio > 0.0) {
                tex1 *= (mixRatio1 / totalRatio);
                tex2 *= (mixRatio2 / totalRatio);
                tex3 *= (mixRatio3 / totalRatio);
            }
            baseColor = (tex1 + tex2 + tex3).rgb;
        } else {
            baseColor = texture(diffuseTexture, TexCoord).rgb;
        }
    } else {
        baseColor = vec3(1.0);
    }
    
    vec3 norm = normalize(Normal);
    // Cálculos de iluminación
    vec3 ambient = 0.15 * baseColor;
    float diff = max(dot(norm, -lightDir), 0.0);
    vec3 diffuse = diff * baseColor;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = vec3(0.3) * spec;
    
    // Cálculo de sombra
    float shadow = ShadowCalculation(FragPosLightSpace, norm, lightDir);
    vec3 lighting = ambient + (1.0 - shadow) * (diffuse + specular);
    
    FragColor = vec4(lighting, 1.0);
}
)";

// Shader para la pasada de profundidad (solo guarda la profundidad)
const char* depthVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 lightSpaceMatrix;
void main()
{
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
)";

const char* depthFragmentShaderSource = R"(
#version 330 core
void main()
{
    // No es necesario escribir color
}
)";

// --- GEOMETRÍA ---
// Definición de un cubo con 36 vértices (cada vértice: posición, normal, coord. de textura)
float cubeVertices[] = {
    // Posición             // Normal           // TexCoord
    // Front face
    -0.5f, -0.5f,  0.5f,     0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,     0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,     0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,     0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,     0.0f,  0.0f,  1.0f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,     0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
    // Back face
    -0.5f, -0.5f, -0.5f,     0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,     0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,     0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,     0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,     0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,     0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
    // Left face
    -0.5f,  0.5f,  0.5f,    -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,    -1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,    -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,    -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,    -1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,    -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
    // Right face
     0.5f,  0.5f,  0.5f,     1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
     0.5f, -0.5f, -0.5f,     1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,     1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,     1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,     1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,     1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
     // Top face
     -0.5f,  0.5f, -0.5f,     0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
     -0.5f,  0.5f,  0.5f,     0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
      0.5f,  0.5f,  0.5f,     0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
      0.5f,  0.5f,  0.5f,     0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
      0.5f,  0.5f, -0.5f,     0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
     -0.5f,  0.5f, -0.5f,     0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
     // Bottom face
     -0.5f, -0.5f, -0.5f,     0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
      0.5f, -0.5f, -0.5f,     0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
      0.5f, -0.5f,  0.5f,     0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
      0.5f, -0.5f,  0.5f,     0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
     -0.5f, -0.5f,  0.5f,     0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
     -0.5f, -0.5f, -0.5f,     0.0f, -1.0f,  0.0f,  1.0f, 1.0f
};

// Un plano para representar el piso (se extiende en X y Z)
float planeVertices[] = {
    // Posición                // Normal       // TexCoords
     10.0f, -2.5f,  10.0f,      0.0f, 1.0f, 0.0f,   10.0f,  0.0f,
    -10.0f, -2.5f,  10.0f,      0.0f, 1.0f, 0.0f,    0.0f,  0.0f,
    -10.0f, -2.5f, -10.0f,      0.0f, 1.0f, 0.0f,    0.0f, 10.0f,

     10.0f, -2.5f,  10.0f,      0.0f, 1.0f, 0.0f,   10.0f,  0.0f,
    -10.0f, -2.5f, -10.0f,      0.0f, 1.0f, 0.0f,    0.0f, 10.0f,
     10.0f, -2.5f, -10.0f,      0.0f, 1.0f, 0.0f,   10.0f, 10.0f
};

// Función para cargar texturas desde archivo
unsigned int loadTexture(const char* path) {
    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true); // Invertir verticalmente
    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);
    if (data) {
        GLenum format;
        if (nrChannels == 1)
            format = GL_RED;
        else if (nrChannels == 3)
            format = GL_RGB;
        else if (nrChannels == 4)
            format = GL_RGBA;
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        // Parámetros de la textura
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data);
    }
    else {
        std::cout << "Error al cargar textura: " << path << std::endl;
        stbi_image_free(data);
    }
    return textureID;
}

// Estructura para la configuración de multitextura
struct MultiTextureConfig {
    bool useMultiTexture;
    int texIndex1;
    int texIndex2;
    int texIndex3;
    float mixRatio1;
    float mixRatio2;
    float mixRatio3;
};

int main() {
    if (!glfwInit()) return -1;

    GLFWwindow* window = glfwCreateWindow(1400, 1200, "Móvil con Luces y Sombras", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    glewInit();

    // Configuración de Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    glEnable(GL_DEPTH_TEST);

    // --- COMPILACIÓN DE SHADERS ---
    auto compileShader = [&](GLenum type, const char* source) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, NULL);
        glCompileShader(shader);
        int success;
        char infoLog[512];
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 512, NULL, infoLog);
            std::cout << "Error al compilar shader: " << infoLog << std::endl;
        }
        return shader;
    };

    // Programa principal (iluminación y sombras)
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    int success;
    char infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "Error al enlazar programa principal: " << infoLog << std::endl;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Programa de profundidad (para shadow mapping)
    GLuint depthVertexShader = compileShader(GL_VERTEX_SHADER, depthVertexShaderSource);
    GLuint depthFragmentShader = compileShader(GL_FRAGMENT_SHADER, depthFragmentShaderSource);
    GLuint depthShaderProgram = glCreateProgram();
    glAttachShader(depthShaderProgram, depthVertexShader);
    glAttachShader(depthShaderProgram, depthFragmentShader);
    glLinkProgram(depthShaderProgram);
    glGetProgramiv(depthShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(depthShaderProgram, 512, NULL, infoLog);
        std::cout << "Error al enlazar programa de profundidad: " << infoLog << std::endl;
    }
    glDeleteShader(depthVertexShader);
    glDeleteShader(depthFragmentShader);

    // --- CONFIGURACIÓN DE BUFFERS PARA EL CUBO ---
    GLuint cubeVAO, cubeVBO;
    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
    // Atributo 0: posición
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Atributo 1: normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // Atributo 2: coord. de textura
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // --- CONFIGURACIÓN DE BUFFERS PARA EL PISO (plano) ---
    GLuint planeVAO, planeVBO;
    glGenVertexArrays(1, &planeVAO);
    glGenBuffers(1, &planeVBO);
    glBindVertexArray(planeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, planeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(planeVertices), planeVertices, GL_STATIC_DRAW);
    // Posición
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // TexCoord
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // --- CARGA DE TEXTURAS ---
    std::vector<unsigned int> textures;
    textures.push_back(loadTexture("textures/wood.jpg"));     // índice 0
    textures.push_back(loadTexture("textures/metal.jpg"));    // índice 1
    textures.push_back(loadTexture("textures/concrete.jpg")); // índice 2
    textures.push_back(loadTexture("textures/grass.jpeg"));   // índice 3
    textures.push_back(loadTexture("textures/stone.jpeg"));    // índice 4

    // Posiciones de los objetos del móvil (como en tu código original)
    glm::vec3 posiciones[12] = {
        glm::vec3(2.0f, -2.0f, 0.0f),
        glm::vec3(-2.0f, -2.0f, 0.0f),
        glm::vec3(0.0f, -2.0f, 2.0f),
        glm::vec3(0.0f, -2.0f, -2.0f),
        glm::vec3(0.0f,  4.0f, 0.0f),
        glm::vec3(-20.0f, -0.5f, 0.0f),
        glm::vec3(20.0f, -0.5f, 0.0f),
        glm::vec3(0.0f, -0.5f, 20.0f),
        glm::vec3(0.0f, -0.5f, -20.0f),
        glm::vec3(0.0f, -0.5f, 0.0f),
        glm::vec3(0.0f, -0.5f, 0.0f),
        glm::vec3(0.0f,  0.5f, 0.0f)
    };

    // Configuración de multitextura para cada objeto (igual que en tu código)
    int cubeTextures[12] = { 0, 1, 2, 3, 4, 0, 1, 2, 3, 0, 1, 2 };
    bool useTextures[12] = { true, true, true, true, true, true, true, true, true, true, true, true };
    MultiTextureConfig multiTexConfigs[12];
    for (int i = 0; i < 12; i++) {
        multiTexConfigs[i] = {
            false,
            i % 5,
            (i + 1) % 5,
            (i + 2) % 5,
            1.0f,
            0.0f,
            0.0f
        };
    }
    // Activar multitextura para los tres primeros objetos
    multiTexConfigs[0].useMultiTexture = true;
    multiTexConfigs[1].useMultiTexture = true;
    multiTexConfigs[2].useMultiTexture = true;

    // --- CONFIGURACIÓN DEL SHADOW MAPPING ---
    const unsigned int SHADOW_WIDTH = 1024, SHADOW_HEIGHT = 1024;
    GLuint depthMapFBO;
    glGenFramebuffers(1, &depthMapFBO);
    GLuint depthMap;
    glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0, 1.0, 1.0, 1.0 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glClearColor(0.6f, 0.8f, 1.0f, 1.0f);

    // --- Parámetros de luz ---
    glm::vec3 lightDir = glm::normalize(glm::vec3(-0.2f, -1.0f, -0.3f));

    // Bucle principal
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Control de cámara 
        if (ImGui::IsKeyDown(ImGuiKey_W))
            wasd_Movement.y -= movementSpeed * deltaTime;
        if (ImGui::IsKeyDown(ImGuiKey_S))
            wasd_Movement.y += movementSpeed * deltaTime;
        if (ImGui::IsKeyDown(ImGuiKey_A))
            wasd_Movement.x += movementSpeed * deltaTime;
        if (ImGui::IsKeyDown(ImGuiKey_D))
            wasd_Movement.x -= movementSpeed * deltaTime;
        if (io.MouseWheel > 0)
            wasd_Movement.z += 200.0f * deltaTime;
        if (io.MouseWheel < 0)
            wasd_Movement.z -= 200.0f * deltaTime;

        if (io.MouseDown[1]) {
            float deltaX = io.MouseDelta.y;
            float deltaY = io.MouseDelta.x;
            Yaw += deltaX * deltaTime * mouseSensitivity;
            Pitch += deltaY * deltaTime * mouseSensitivity;
        }

        // Matriz de vista: primero trasladamos (para "alejar" la cámara) y luego rotamos
        glm::mat4 view = glm::mat4(1.0f);
        view = glm::translate(view, glm::vec3(wasd_Movement.x, wasd_Movement.y, -18.0f + wasd_Movement.z));
        view = glm::rotate(view, Pitch, glm::vec3(0.0f, 1.0f, 0.0f));
        view = glm::rotate(view, Yaw, glm::vec3(1.0f, 0.0f, 0.0f));

        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);

        // --- PASADA 1: RENDERIZADO DEL MAPA DE SOMBRAS ---
        // Configuramos la cámara de la luz (usamos proyección ortográfica)
        float near_plane = 1.0f, far_plane = 20.0f;
        glm::mat4 lightProjection = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, near_plane, far_plane);
        glm::mat4 lightView = glm::lookAt(-lightDir * 10.0f, glm::vec3(0.0f), glm::vec3(0.0, 1.0, 0.0));
        glm::mat4 lightSpaceMatrix = lightProjection * lightView;

        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(depthShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(depthShaderProgram, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

        // Renderizar cada objeto (móvil) en la pasada de profundidad
        glBindVertexArray(cubeVAO);
        float angle = currentFrame * 0.4f;
        for (int i = 0; i < 12; i++) {
            glm::mat4 model = glm::mat4(1.0f);
            // Ajuste de escala segun el objeto
            if (i >= 5 && i < 9)
                model = scale(glm::vec3(0.1f, 2.0f, 0.1f));
            else if (i == 9)
                model = scale(glm::vec3(4.0f, 0.1f, 0.1f));
            else if (i == 10)
                model = scale(glm::vec3(0.1f, 0.1f, 4.0f));
            else if (i == 11)
                model = scale(glm::vec3(0.1f, 4.0f, 0.1f));
            model = glm::translate(model, posiciones[i]);
            model = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f)) * model;
            glUniformMatrix4fv(glGetUniformLocation(depthShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
        // Renderizar el piso (plano)
        glBindVertexArray(planeVAO);
        glm::mat4 modelFloor = glm::mat4(1.0f);
        glUniformMatrix4fv(glGetUniformLocation(depthShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(modelFloor));
     
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // --- PASADA 2: RENDERIZADO DE LA ESCENA CON SOMBRAS ---
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(shaderProgram);
        // Enviar matrices de cámara y luz
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
        // Enviar posición de la cámara (para el cálculo especular)
        glm::vec3 camPos = glm::vec3(wasd_Movement.x, wasd_Movement.y, -18.0f + wasd_Movement.z);
        glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1, glm::value_ptr(camPos));
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightDir"), 1, glm::value_ptr(lightDir));
        // Asignar las texturas: se utilizarán las unidades 0-2 para el objeto y la 3 para el mapa de sombras.
        glUniform1i(glGetUniformLocation(shaderProgram, "diffuseTexture"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "texture1"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "texture2"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram, "texture3"), 2);
        glUniform1i(glGetUniformLocation(shaderProgram, "shadowMap"), 3);

        // Renderizar cada objeto del móvil
        glBindVertexArray(cubeVAO);
        for (int i = 0; i < 12; i++) {
            glm::mat4 model = glm::mat4(1.0f);
            if (i >= 5 && i < 9)
                model = scale(glm::vec3(0.1f, 2.0f, 0.1f));
            else if (i == 9)
                model = scale(glm::vec3(4.0f, 0.1f, 0.1f));
            else if (i == 10)
                model = scale(glm::vec3(0.1f, 0.1f, 4.0f));
            else if (i == 11)
                model = scale(glm::vec3(0.1f, 4.0f, 0.1f));
            model = glm::translate(model, posiciones[i]);
            model = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f)) * model;
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            // Configurar uso de texturas y multitextura según el objeto
            glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), useTextures[i]);
            glUniform1i(glGetUniformLocation(shaderProgram, "useMultiTexture"), multiTexConfigs[i].useMultiTexture);
            if (multiTexConfigs[i].useMultiTexture && useTextures[i]) {
                glUniform1f(glGetUniformLocation(shaderProgram, "mixRatio1"), multiTexConfigs[i].mixRatio1);
                glUniform1f(glGetUniformLocation(shaderProgram, "mixRatio2"), multiTexConfigs[i].mixRatio2);
                glUniform1f(glGetUniformLocation(shaderProgram, "mixRatio3"), multiTexConfigs[i].mixRatio3);
                // Vincular texturas para multitextura (unidades 0,1,2)
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, textures[multiTexConfigs[i].texIndex1]);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, textures[multiTexConfigs[i].texIndex2]);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, textures[multiTexConfigs[i].texIndex3]);
            }
            else {
                // Uso de una sola textura (diffuseTexture)
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, textures[cubeTextures[i]]);
            }
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, depthMap);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        // Renderizar el piso
        glBindVertexArray(planeVAO);
        glm::mat4 modelFloorScene = glm::mat4(1.0f);  // Renombrada para evitar redefinición
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(modelFloorScene));
        glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), true);
        glUniform1i(glGetUniformLocation(shaderProgram, "useMultiTexture"), false);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures[3]);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, depthMap);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // --- INTERFAZ IMGUI ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (ImGui::Button("Reset View")) {
                Yaw = 0.0f;
                Pitch = 0.0f;
                wasd_Movement = { 0.f, 0.f, 0.0f };
            }
            ImGui::SliderFloat("Mouse Sensitivity", &mouseSensitivity, 0.1f, 2.0f);
            ImGui::Separator();
            ImGui::Text("Texture Settings:");
            const char* textureNames[] = { "Wood", "Metal", "Concrete", "Grass", "Stone" };
            for (int i = 0; i < 5; i++) {
                ImGui::PushID(i);
                ImGui::Checkbox("Use Texture", &useTextures[i]);
                if (useTextures[i]) {
                    if (ImGui::Combo("Texture", &cubeTextures[i], textureNames, IM_ARRAYSIZE(textureNames))) {
     
                    }
                }
                ImGui::PopID();
                ImGui::Separator();
            }
            ImGui::Separator();
            ImGui::Text("Multitexture Settings:");
            for (int i = 0; i < 3; i++) {
                ImGui::PushID(i + 100);
                const char* cube_name = "cube_x";
                ImGui::Checkbox(cube_name, &multiTexConfigs[i].useMultiTexture);
                if (multiTexConfigs[i].useMultiTexture) {
                    ImGui::Combo("Primary Texture", &multiTexConfigs[i].texIndex1, textureNames, IM_ARRAYSIZE(textureNames));
                    ImGui::Combo("Secondary Texture", &multiTexConfigs[i].texIndex2, textureNames, IM_ARRAYSIZE(textureNames));
                    ImGui::Combo("Tertiary Texture", &multiTexConfigs[i].texIndex3, textureNames, IM_ARRAYSIZE(textureNames));
                    ImGui::SliderFloat("Primary Mix", &multiTexConfigs[i].mixRatio1, 0.0f, 1.0f);
                    ImGui::SliderFloat("Secondary Mix", &multiTexConfigs[i].mixRatio2, 0.0f, 1.0f);
                    ImGui::SliderFloat("Tertiary Mix", &multiTexConfigs[i].mixRatio3, 0.0f, 1.0f);
                    if (ImGui::Button("Blend Equal")) {
                        multiTexConfigs[i].mixRatio1 = 0.33f;
                        multiTexConfigs[i].mixRatio2 = 0.33f;
                        multiTexConfigs[i].mixRatio3 = 0.33f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Primary Dominant")) {
                        multiTexConfigs[i].mixRatio1 = 0.7f;
                        multiTexConfigs[i].mixRatio2 = 0.2f;
                        multiTexConfigs[i].mixRatio3 = 0.1f;
                    }
                }
                ImGui::PopID();
                ImGui::Separator();
            }
            ImGui::Text("Camera Controls:");
            ImGui::BulletText("WASD - Move camera");
            ImGui::BulletText("Right Click - Rotate camera");
            ImGui::BulletText("Mouse Wheel - Zoom in/out");
        }
        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Limpieza de recursos
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);
    glDeleteVertexArrays(1, &planeVAO);
    glDeleteBuffers(1, &planeVBO);
    glDeleteProgram(shaderProgram);
    glDeleteProgram(depthShaderProgram);
    for (unsigned int tex : textures) {
        glDeleteTextures(1, &tex);
    }
    glDeleteTextures(1, &depthMap);
    glDeleteFramebuffers(1, &depthMapFBO);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

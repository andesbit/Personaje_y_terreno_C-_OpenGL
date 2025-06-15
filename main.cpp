
/*
    Copyright 2025 Adolfo Cárdenas P.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Player.hpp"

// --- Variables globales para las texturas ---
GLuint floorTextureID;
GLuint heightmapTextureID;
GLuint sandTextureID;
GLuint rockTextureID;
GLuint snowTextureID;

//-------NEWOBJ-----

GLuint newObjectTextureID; // ID de la textura para tu PNG
GLuint newObjectVAO, newObjectVBO; // VAO y VBO para un simple quad (o un modelo más complejo si tienes)
const char* objectVertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 4) in vec2 aTexCoords;

    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;

    out vec2 TexCoords;

    void main() {
        gl_Position = projection * view * model * vec4(aPos, 1.0);
        TexCoords = aTexCoords;
    }
)";

// --- ARCHIVO: object_fragment_shader.glsl (o en tu .cpp) ---
const char* objectFragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    in vec2 TexCoords;

    uniform sampler2D ourTexture; // La textura del PNG
    // Si quieres iluminación simple, puedes añadir uniforms de luz aquí también
    // uniform vec3 lightColor;
    // uniform float ambientStrength;

    void main() {
        FragColor = texture(ourTexture, TexCoords); // Simplemente muestra la textura
        // Si tienes iluminación simple:
        // vec3 finalColor = texture(ourTexture, TexCoords).rgb * ambientStrength * lightColor;
        // FragColor = vec4(finalColor, texture(ourTexture, TexCoords).a); // Asegúrate de manejar el canal alfa
    }
)";
//----------------------------------------------------------------------


unsigned char* heightmapCpuData = nullptr; 
int heightmapWidth, heightmapHeight, heightmapNrChannels; // Añade estas líneas

// Variables globales para VAO/VBO/EBO del suelo ---
GLuint floorVAO, floorVBO, floorEBO;

unsigned char *h_data;

// Helper function to check for OpenGL errors
void checkGLError(const std::string& stage) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "OpenGL Error at " << stage << ": " << err << std::endl;
    }
}

// Función para verificar errores de compilación/linkeo de shaders
void checkShaderCompileErrors(GLuint shader, std::string type) {
    GLint success;
    GLchar infoLog[1024];
    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            std::cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            std::cerr << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
        }
    }
}

const char* floorVertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aNormal;
    layout (location = 4) in vec2 aTexCoords;

    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;
    
    uniform sampler2D heightmap;    
    uniform float heightScale;
    uniform float sampleDist; // Nuevo uniform
    uniform float terrainYOffset;
    uniform vec2 grassTexRepeat;

    out vec3 Normal;
    out vec3 Normal2;
    out vec3 FragPos;
    out vec2 TexCoords;

    void main() {
        float heightValue = texture(heightmap, aTexCoords).r; 

        vec3 newPos = aPos;
        newPos.y = terrainYOffset + heightValue * heightScale; 
        
        gl_Position = projection * view * model * vec4(newPos, 1.0);
        FragPos = vec3(model * vec4(newPos, 1.0));

        //float sampleDist = 0.001; 
        float sampleDist2 = 0.0001; 
        // Para evitar problemas con las normales en los bordes del heightmap
        // Se asegura que los muestreos vecinos no se salgan de 0-1
        vec2 uv_clamped = clamp(aTexCoords, vec2(sampleDist, sampleDist), vec2(1.0 - sampleDist, 1.0 - sampleDist));

        float hL = texture(heightmap, uv_clamped - vec2(sampleDist, 0.0)).r * heightScale;
        float hR = texture(heightmap, uv_clamped + vec2(sampleDist, 0.0)).r * heightScale;
        float hD = texture(heightmap, uv_clamped - vec2(0.0, sampleDist)).r * heightScale;
        float hU = texture(heightmap, uv_clamped + vec2(0.0, sampleDist)).r * heightScale;

        // Calcular la normal usando el producto cruz (cross product)
        // Normal = normalize(cross(vec3(dx, hR-hL, 0), vec3(0, hU-hD, dz)))
        // Simplificado para un heightmap:
        vec3 normal = normalize(vec3(hL - hR, 2.0 * sampleDist, hD - hU)); // (dz para GLSL es 'y' del vector, dy es 'z')
        Normal = mat3(transpose(inverse(model))) * normal;

        hL = texture(heightmap, uv_clamped - vec2(sampleDist2, 0.0)).r * heightScale;
        hR = texture(heightmap, uv_clamped + vec2(sampleDist2, 0.0)).r * heightScale;
        hD = texture(heightmap, uv_clamped - vec2(0.0, sampleDist2)).r * heightScale;
        hU = texture(heightmap, uv_clamped + vec2(0.0, sampleDist2)).r * heightScale;
        //vec3 normal2 = normalize(vec3(hL - hR, 2.0 * , hD - hU)); // (dz para GLSL es 'y' del vector, dy es 'z')
        vec3 normal2 = normalize(vec3(hL - hR, 0.02, hD - hU)); // (dz para GLSL es 'y' del vector, dy es 'z')
        
        Normal2 = mat3(transpose(inverse(model))) * normal2;
        TexCoords = aTexCoords * grassTexRepeat;
    }
)";

const char* floorFragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    in vec3 Normal;    
    in vec3 Normal2;
    in vec3 FragPos;
    in vec2 TexCoords;

    uniform vec3 lightPos;
    uniform vec3 viewPos;
    
    uniform sampler2D ourTexture;
    uniform sampler2D sandTexture;
    uniform sampler2D rockTexture;
    uniform sampler2D snowTexture;
    
    uniform vec3 lightColor;
    uniform float ambientStrength;
    uniform float diffuseStrength;
    
    void main() {
        vec3 norm = normalize(Normal);
        vec3 norm2 = normalize(Normal2);
        vec3 lightDir = normalize(lightPos - FragPos);

        // Iluminación
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * lightColor * diffuseStrength; 
        vec3 ambient = ambientStrength * lightColor; 

        // Texturas
        vec3 texColor = texture(ourTexture, TexCoords).rgb;
        vec3 sandColor = texture(sandTexture, TexCoords).rgb;
        vec3 rockColor = texture(rockTexture, TexCoords * 15.0).rgb; // Más tiling para roca
        vec3 snowColor = texture(snowTexture, TexCoords).rgb;

        // Mezcla arena/hierba (basada en altura)
        float sandBlendFactor = smoothstep(-16.0f, -15.0f, FragPos.y);
        vec3 finalColor = mix(sandColor, texColor, sandBlendFactor);

        // --- CÁLCULO CORRECTO PARA ROCA EN PENDIENTES ---
        //float slope = 1.0 - abs(dot(norm, vec3(0.0, 1.0, 0.0))); // 0=plano, 1=vertical
        float slope = 1.0 - abs(dot(norm2, vec3(0.0, 1.0, 0.0))); // 0=plano, 1=vertical
                
        // Ángulos de transición (en valores de slope, no grados)
        
        float rockStartSlope = 1.0-cos(radians(70.0)); // Approx 0.5
        float rockFullSlope = 1.0-cos(radians(82.0));  // Approx 0.826    
        
        float rockBlendFactor = smoothstep(rockStartSlope, rockFullSlope, slope);
        
        // Añadir variación con ruido
        float noise = fract(sin(dot(FragPos.xz, vec2(12.9898, 78.233))) * 43758.5453) * 0.15;
        rockBlendFactor = clamp(rockBlendFactor + noise, 0.0, 1.0);
                
        finalColor = mix(finalColor, rockColor, rockBlendFactor*0.8);


         // Mezcla de Nieve (basada en altura, sobre todo lo demás)
        // Nieve en las zonas más altas
        //float snowBlendFactor = smoothstep(heightBlendStartSnow, heightBlendEndSnow, FragPos.y);
        float snowBlendFactor = smoothstep(0.0f, 1.0f, FragPos.y);
        finalColor = mix(finalColor, snowColor, snowBlendFactor); // Mezcla nieve con lo que ya tenías



        // Resultado con iluminación
        
        //FragColor = vec4(slope, slope, slope, 1.0);
        FragColor = vec4(finalColor * (ambient + diffuse), 1.0);
        //FragColor = vec4(normalize(Normal) * 0.5 + 0.5, 1.0);//para ver las normales directamente
    }   
)";

// Funciones para generar la malla del terreno ---
// Genera una cuadrícula de vértices para el terreno
std::vector<float> generateTerrainGridVertices(int resolutionX, int resolutionZ, float terrainSizeX, float terrainSizeZ, float baseHeight) {
    std::vector<float> vertices;
    float stepX = terrainSizeX / (resolutionX - 1);
    float stepZ = terrainSizeZ / (resolutionZ - 1);

    for (int z = 0; z < resolutionZ; ++z) {
        for (int x = 0; x < resolutionX; ++x) {
            float posX = -terrainSizeX / 2.0f + x * stepX;
            float posZ = -terrainSizeZ / 2.0f + z * stepZ;
            float posY = baseHeight; // La altura Y será modificada por el heightmap en el shader

            // Normal inicial (apuntando hacia arriba, se recalculará en el shader)
            float normX = 0.0f;
            float normY = 1.0f;
            float normZ = 0.0f;

            // Coordenadas de textura (UVs de 0.0 a 1.0 para el heightmap y la repetición de hierba)
            float texU = (float)x / (resolutionX - 1);
            float texV = (float)z / (resolutionZ - 1);

            // Añadir los 8 floats por vértice: Posición (3), Normal (3), TexCoords (2)
            vertices.push_back(posX);
            vertices.push_back(posY);
            vertices.push_back(posZ);
            vertices.push_back(normX);
            vertices.push_back(normY);
            vertices.push_back(normZ);
            vertices.push_back(texU);
            vertices.push_back(texV);
        }
    }
    return vertices;
}

// Genera los índices para una cuadrícula de terreno (para dibujar con EBO)
std::vector<unsigned int> generateTerrainGridIndices(int resolutionX, int resolutionZ) {
    std::vector<unsigned int> indices;
    for (int z = 0; z < resolutionZ - 1; ++z) {
        for (int x = 0; x < resolutionX - 1; ++x) {
            unsigned int topLeft = z * resolutionX + x;
            unsigned int topRight = z * resolutionX + x + 1;
            unsigned int bottomLeft = (z + 1) * resolutionX + x;
            unsigned int bottomRight = (z + 1) * resolutionX + x + 1;

            // Primer triángulo del quad (Top-Left, Bottom-Left, Top-Right)
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Segundo triángulo del quad (Top-Right, Bottom-Left, Bottom-Right)
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
    return indices;
}

float getTerrainHeight(float worldX, float worldZ, float terrainWidth, float terrainDepth, float terrainYOffset, 
                       float heightScale, const unsigned char* heightmapData, int h_width, int h_height, int h_channels) {
    if (!heightmapData || h_width == 0 || h_height == 0) {
        return terrainYOffset; // Retorna la altura base si el heightmap no está cargado
    }

    // 1. Convertir coordenadas del mundo (X, Z) a UV del heightmap (0.0 a 1.0)
    float normalizedX = (worldX + terrainWidth / 2.0f) / terrainWidth;
    float normalizedZ = (worldZ + terrainDepth / 2.0f) / terrainDepth;

    // Asegurarse de que las coordenadas UV estén dentro del rango [0.0, 1.0]
    normalizedX = glm::clamp(normalizedX, 0.0f, 1.0f);
    normalizedZ = glm::clamp(normalizedZ, 0.0f, 1.0f);

    // 2. Convertir UV a coordenadas flotantes de píxel del heightmap
    // Multiplicamos por (h_width - 1) porque las coordenadas de píxel van de 0 a (resolución-1)
    float pixelX_float = normalizedX * (h_width - 1);
    float pixelZ_float = normalizedZ * (h_height - 1);

    // 3. Obtener los 4 píxeles enteros circundantes
    int x1 = static_cast<int>(floor(pixelX_float));
    int x2 = static_cast<int>(ceil(pixelX_float));
    int z1 = static_cast<int>(floor(pixelZ_float));
    int z2 = static_cast<int>(ceil(pixelZ_float));

    // Clamp para asegurar que no nos salimos de los límites de la imagen
    x1 = glm::clamp(x1, 0, h_width - 1);
    x2 = glm::clamp(x2, 0, h_width - 1);
    z1 = glm::clamp(z1, 0, h_height - 1);
    z2 = glm::clamp(z2, 0, h_height - 1);

    // 4. Obtener los valores de altura (0.0-1.0) de los 4 píxeles
    // Nota: La lectura de stbi_image es a menudo de arriba a abajo, de izquierda a derecha.
    // h_data[ (y * width + x) * channels ]
    auto getPixelHeightValue = [&](int x, int z) {
        // Asegúrate de que el índice no exceda los límites
        unsigned int index = (z * h_width + x) * h_channels;
        if (index >= h_width * h_height * h_channels) {
            return 0.0f; // Valor por defecto o error si el índice está fuera de rango
        }
        return static_cast<float>(heightmapData[index]) / 255.0f;
    };

    float h00 = getPixelHeightValue(x1, z1); // Top-Left
    float h10 = getPixelHeightValue(x2, z1); // Top-Right
    float h01 = getPixelHeightValue(x1, z2); // Bottom-Left
    float h11 = getPixelHeightValue(x2, z2); // Bottom-Right

    // 5. Calcular los pesos de interpolación
    float tx = pixelX_float - x1; // Fracción entre x1 y x2
    float tz = pixelZ_float - z1; // Fracción entre z1 y z2

    // 6. Interpolación bilineal
    // Interpolar a lo largo del eje X (horizontal)
    float interpolatedX1 = glm::mix(h00, h10, tx); // Interpolación entre (x1,z1) y (x2,z1)
    float interpolatedX2 = glm::mix(h01, h11, tx); // Interpolación entre (x1,z2) y (x2,z2)

    // Interpolar a lo largo del eje Z (vertical)
    float finalHeightValue = glm::mix(interpolatedX1, interpolatedX2, tz);

    // 7. Aplicar la escala y el offset del terreno
    return terrainYOffset + finalHeightValue * heightScale;
}
// Main function
int main()
{
    std::cout << "Main: Starting GLFW initialization." << std::endl;
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    checkGLError("GLFW Init");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Animated Model", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    std::cout << "Main: GLFW context created." << std::endl;
    checkGLError("GLFW Context Creation");

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }
    std::cout << "Main: GLEW initialized." << std::endl;
    checkGLError("GLEW Init");

    glEnable(GL_DEPTH_TEST);
    std::cout << "Main: Depth test enabled." << std::endl;
    checkGLError("Enable Depth Test");

    glClearColor(0.2f, 0.3f, 0.8f, 1.0f); // Blue background
    checkGLError("glClearColor");

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    std::cout << "Main: Viewport set to " << width << "x" << height << std::endl;
    checkGLError("glViewport");

    // --- Carga de personajes ---
    std::vector<std::unique_ptr<AnimatedModel>> characters;
    
    // Carga la primera instancia del personaje principal (el controlable)
    std::cout << "Main: Attempting to create AnimatedModel instance for: Resources/model.dae (Player Character)" << std::endl;
    characters.push_back(std::make_unique<AnimatedModel>("Resources/model.dae"));
    if (characters.back()->shaderProgram == 0) { // Check if loading failed
        std::cerr << "Error: Failed to load model Resources/model.dae. Exiting." << std::endl;
        return -1; 
    }
    std::cout << "Main: AnimatedModel instance created for player character." << std::endl;
    checkGLError("AnimatedModel creation for player character");
    
    if (characters.empty()) {
        std::cerr << "No characters loaded. Exiting." << std::endl;
        glfwTerminate();
        return -1;
    }

    // El índice 0 siempre será el personaje principal (el controlable)
    int currentCharacterIndex = 0; 
    // --- Fin de carga de personajes ---

    // --- Floor Shader Program Setup ---
    GLuint floorVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(floorVertexShader, 1, &floorVertexShaderSource, nullptr);
    glCompileShader(floorVertexShader);
    checkShaderCompileErrors(floorVertexShader, "VERTEX");

    GLuint floorFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(floorFragmentShader, 1, &floorFragmentShaderSource, nullptr);
    glCompileShader(floorFragmentShader);
    checkShaderCompileErrors(floorFragmentShader, "FRAGMENT");

    GLuint floorShaderProgram = glCreateProgram();
    glAttachShader(floorShaderProgram, floorVertexShader);
    glAttachShader(floorShaderProgram, floorFragmentShader);
    glLinkProgram(floorShaderProgram);
    checkShaderCompileErrors(floorShaderProgram, "PROGRAM");

    glDeleteShader(floorVertexShader);
    glDeleteShader(floorFragmentShader);
    checkGLError("Floor Shader Program Setup");
    // --- End Floor Shader Program Setup ---





//----------------------------------------------
    int imgWidth, imgHeight, imgNrChannels;
//-----------------------------------------------




    // --- CAMBIO: Generación de la malla del terreno (vértices e índices) ---
    // Puedes ajustar estos valores para cambiar el tamaño y detalle del terreno
    int terrainResolutionX = 256; // Por ejemplo, un vértice por píxel del heightmap si es 256x256
    int terrainResolutionZ = 256; 
    float terrainWidth = 512.0f; // Tamaño del terreno en unidades de mundo
    float terrainDepth = 512.0f;
    float terrainBaseY = -16.01f; // Altura inicial del plano antes de deformación


    // Para la escala de altura, usa el mismo valor que pasas al shader (0.5f)
    float currentHeightScale = 30.0f; // Declara esta variable y úsala para el shader también

    // Calcula la altura inicial del terreno en el centro (0,0) donde quieres que empiece el personaje
    float initialTerrainHeight = getTerrainHeight(
        0.0f,                  // Posición X inicial del personaje (centro del terreno)
        0.0f,                  // Posición Z inicial del personaje (centro del terreno)
        terrainWidth,         
        terrainDepth,         
        terrainBaseY,         
        currentHeightScale,    // ¡IMPORTANTE! Usar la misma escala que en el shader
        heightmapCpuData,     
        heightmapWidth,
        heightmapHeight,
        heightmapNrChannels
    );

    std::vector<float> floorVerticesVec = generateTerrainGridVertices(terrainResolutionX, terrainResolutionZ, terrainWidth, terrainDepth, terrainBaseY);
    std::vector<unsigned int> floorIndicesVec = generateTerrainGridIndices(terrainResolutionX, terrainResolutionZ);
    std::cout << "DEBUG: Terreno generado con " << floorVerticesVec.size() / 8 << " vértices y " << floorIndicesVec.size() / 3 << " triángulos." << std::endl;

    // --- Floor VAO/VBO/EBO Setup ---
    std::cout << "Main: Generating Floor VAO, VBO, and EBO." << std::endl;
    glGenVertexArrays(1, &floorVAO);
    glGenBuffers(1, &floorVBO);
    glGenBuffers(1, &floorEBO); // --- CAMBIO: Generar EBO ---

    std::cout << "DEBUG: floorVAO ID = " << floorVAO << ", floorVBO ID = " << floorVBO << ", floorEBO ID = " << floorEBO << std::endl;
    checkGLError("glGenVertexArrays/glGenBuffers/glGenBuffers for floor");

    std::cout << "Main: Binding Floor VAO: " << floorVAO << std::endl;
    glBindVertexArray(floorVAO);
    checkGLError("glBindVertexArray for floor");

    std::cout << "Main: Binding Floor VBO: " << floorVBO << std::endl;
    glBindBuffer(GL_ARRAY_BUFFER, floorVBO);
    std::cout << "DEBUG: sizeof(floorVerticesVec) = " << floorVerticesVec.size() * sizeof(float) << " bytes" << std::endl; // Tamaño en bytes del vector
    glBufferData(GL_ARRAY_BUFFER, floorVerticesVec.size() * sizeof(float), floorVerticesVec.data(), GL_STATIC_DRAW);
    checkGLError("glBufferData for floor VBO");

    // Enlazar y enviar datos al EBO ---
    std::cout << "Main: Binding Floor EBO: " << floorEBO << std::endl;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, floorEBO);
    std::cout << "DEBUG: sizeof(floorIndicesVec) = " << floorIndicesVec.size() * sizeof(unsigned int) << " bytes" << std::endl;
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, floorIndicesVec.size() * sizeof(unsigned int), floorIndicesVec.data(), GL_STATIC_DRAW);
    checkGLError("glBufferData for floor EBO");    

    // Atributos de vértice para el suelo (aPos, aNormal, aTexCoords)
    std::cout << "Main: Setting up Floor Vertex Attributes." << std::endl;
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); // aPos (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); // aNormal (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); // aTexCoords (location 4)
    glEnableVertexAttribArray(4);
    
    checkGLError("glVertexAttribPointer/glEnableVertexAttribArray for floor");

    std::cout << "Main: Unbinding Floor VAO." << std::endl;
    glBindVertexArray(0); 
    checkGLError("glBindVertexArray 0 for floor");

    // --- Floor Texture (grass.png) ---
    glGenTextures(1, &floorTextureID);
    checkGLError("glGenTextures for floor");

    glBindTexture(GL_TEXTURE_2D, floorTextureID);
    checkGLError("glBindTexture for floor");

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    checkGLError("glTexParameter for floor");






//-------NEWOBJ-----



  GLuint objectVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(objectVertexShader, 1, &objectVertexShaderSource, nullptr);
    glCompileShader(objectVertexShader);
    checkShaderCompileErrors(objectVertexShader, "OBJECT_VERTEX");

    GLuint objectFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(objectFragmentShader, 1, &objectFragmentShaderSource, nullptr);
    glCompileShader(objectFragmentShader);
    checkShaderCompileErrors(objectFragmentShader, "OBJECT_FRAGMENT");

    GLuint objectShaderProgram = glCreateProgram();
    glAttachShader(objectShaderProgram, objectVertexShader);
    glAttachShader(objectShaderProgram, objectFragmentShader);
    glLinkProgram(objectShaderProgram);
    checkShaderCompileErrors(objectShaderProgram, "OBJECT_PROGRAM");

    glDeleteShader(objectVertexShader);
    glDeleteShader(objectFragmentShader);
    checkGLError("Object Shader Program Setup");




   

  

//-----------------------------------------------------------




//-------NEWOBJ-----

 // Crear un simple quad para el PNG
    float quadVertices[] = {
        // Posiciones        // Coordenadas de Textura
        -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, // Bottom-left
         0.5f, -0.5f, 0.0f,  1.0f, 0.0f, // Bottom-right
         0.5f,  0.5f, 0.0f,  1.0f, 1.0f, // Top-right
        -0.5f,  0.5f, 0.0f,  0.0f, 1.0f  // Top-left
    };
    unsigned int quadIndices[] = {
        0, 1, 2, // Primer triángulo
        2, 3, 0  // Segundo triángulo
    };

    glGenVertexArrays(1, &newObjectVAO);
    glGenBuffers(1, &newObjectVBO);
    // Para el quad simple, usaremos GL_ARRAY_BUFFER. Si quieres EBO, necesitarías glGenBuffers(1, &newObjectEBO);
    // y glBufferData(GL_ELEMENT_ARRAY_BUFFER, ...). Para un quad de 4 vértices, no es estrictamente necesario EBO.

    glBindVertexArray(newObjectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, newObjectVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // Atributos de vértice para el quad (posición y coordenadas de textura)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); // Asumiendo location 4 para TexCoords
    glEnableVertexAttribArray(4);

    glBindVertexArray(0); // Desenlazar VAO





    

    unsigned char *data = stbi_load("Resources/grass2.png", &imgWidth, &imgHeight, &imgNrChannels, 0);
    if (data)
    {
        GLenum format = GL_RGB;
        if (imgNrChannels == 1) format = GL_RED;
        else if (imgNrChannels == 3) format = GL_RGB;
        else if (imgNrChannels == 4) format = GL_RGBA;

        glTexImage2D(GL_TEXTURE_2D, 0, format, imgWidth, imgHeight, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
        std::cout << "Floor texture loaded: Resources/grass.png" << std::endl;
    }
    else
    {
        std::cerr << "Failed to load floor texture: Resources/grass.png. Using solid color or default." << std::endl;
    }
    checkGLError("Texture loading for floor");


//-------NEWOBJ-----
 // Cargar la nueva textura PNG para el objeto
    glGenTextures(1, &newObjectTextureID);
    glBindTexture(GL_TEXTURE_2D, newObjectTextureID);

    // Parámetros de textura (puedes ajustar según necesites)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // Clamp para que no se repita
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Cargar los datos de la imagen PNG
    unsigned char *object_data = stbi_load("Resources/coco.png", &imgWidth, &imgHeight, &imgNrChannels, 0);
    if (object_data)
    {
        GLenum format = GL_RGB;
        if (imgNrChannels == 4) format = GL_RGBA; // Si tu PNG tiene transparencia
        else if (imgNrChannels == 3) format = GL_RGB;
        else if (imgNrChannels == 1) format = GL_RED;

        glTexImage2D(GL_TEXTURE_2D, 0, format, imgWidth, imgHeight, 0, format, GL_UNSIGNED_BYTE, object_data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(object_data);
        std::cout << "New object texture loaded: Resources/coco.png" << std::endl;
    }
    else
    {
        std::cerr << "Failed to load new object texture: Resources/coco.png. Using default or solid color." << std::endl;
    }
    checkGLError("New object texture loading");






    // Cargar Sand Texture
    glGenTextures(1, &sandTextureID);
    glBindTexture(GL_TEXTURE_2D, sandTextureID);
    // Configurar parámetros (GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, etc.)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Cargar datos de la imagen (ej. Resources/sand.png)
    unsigned char *sand_data = stbi_load("Resources/sand.png", &imgWidth, &imgHeight, &imgNrChannels, 0);
    if (sand_data) {
        GLenum format = GL_RGB; // Ajusta según el formato de tu imagen
        if (imgNrChannels == 4) format = GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, format, imgWidth, imgHeight, 0, format, GL_UNSIGNED_BYTE, sand_data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(sand_data);
        std::cout << "sand texture loaded: Resources/sand.png" << std::endl;
    } else { /* Error handling */ }
    checkGLError("sand Texture Loading");

    // Cargar Rock Texture
    glGenTextures(1, &rockTextureID);
    glBindTexture(GL_TEXTURE_2D, rockTextureID);
    // Configurar parámetros (GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, etc.)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Cargar datos de la imagen (ej. Resources/rock.png)
    
    unsigned char *rock_data = stbi_load("Resources/rock.png", &imgWidth, &imgHeight, &imgNrChannels, 0);
    if (rock_data) {
        GLenum format = GL_RGB; // Ajusta según el formato de tu imagen
        if (imgNrChannels == 4) format = GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, format, imgWidth, imgHeight, 0, format, GL_UNSIGNED_BYTE, rock_data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(rock_data);
        std::cout << "Rock texture loaded: Resources/rock.png" << std::endl;
    } else { /* Error handling */ }
    checkGLError("Rock Texture Loading");


    // Cargar Snow Texture
    glGenTextures(1, &snowTextureID);
    glBindTexture(GL_TEXTURE_2D, snowTextureID);
    // Configurar parámetros (GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, etc.)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Cargar datos de la imagen (ej. Resources/snow.png)
    unsigned char *snow_data = stbi_load("Resources/snow.png", &imgWidth, &imgHeight, &imgNrChannels, 0);
    if (snow_data) {
        GLenum format = GL_RGB; // Ajusta según el formato de tu imagen
        if (imgNrChannels == 4) format = GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, format, imgWidth, imgHeight, 0, format, GL_UNSIGNED_BYTE, snow_data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(snow_data);
        std::cout << "snow texture loaded: Resources/snow.png" << std::endl;
    } else { /* Error handling */ }
    checkGLError("snow Texture Loading");


    // --- Carga del Heightmap (heightmap.png) ---
    glGenTextures(1, &heightmapTextureID);
    glBindTexture(GL_TEXTURE_2D, heightmapTextureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int h_width, h_height, h_nrChannels;
    h_data = stbi_load("heightmap.png", &h_width, &h_height, &h_nrChannels, 0);
    if (h_data) {
        GLenum h_format = GL_RGB;
        if (h_nrChannels == 1)      h_format = GL_RED;
        else if (h_nrChannels == 3) h_format = GL_RGB;
        else if (h_nrChannels == 4) h_format = GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, h_format, h_width, h_height, 0, h_format, GL_UNSIGNED_BYTE, h_data);
        glGenerateMipmap(GL_TEXTURE_2D);        
        std::cout << "Heightmap texture loaded: heightmap.png (Width: " << h_width << ", Height: " << h_height << ", Channels: " << h_nrChannels << ")" << std::endl;
        heightmapCpuData = h_data; // ¡IMPORTANTE! Ahora heightmapCpuData apunta a estos datos
        heightmapWidth = h_width;
        heightmapHeight = h_height;
        heightmapNrChannels = h_nrChannels;
    } else {
        std::cerr << "Failed to load heightmap: heightmap.png. Make sure it's in the program directory." << std::endl;
    }
    checkGLError("Heightmap texture loading");

    
    //float heightmapPixelSize = 1.0f / heightmapWidth; // Asumiendo width = height para simplificar
    
    float heightmapPixelSize = 1.0f / heightmapWidth;//Produce normales más suaves y realistas.

    // Establece la posición inicial del personaje en X=0, Z=0 y la altura Y del terreno
    // Agrega un pequeño offset (+0.5f o +1.0f) si el pivote de tu modelo no está en la base de los pies.
    glm::vec3 characterPosition = glm::vec3(0.0f, initialTerrainHeight + 0.5f, 0.0f); 
    float characterRotationY = glm::radians(180.0f); 
   
    // Ajusta el offset de la cámara para que esté más cerca y mire ligeramente hacia abajo.
    // Prueba con estos valores para una vista típica en tercera persona.
    glm::vec3 cameraOffset = glm::vec3(0.0f, 5.0f, 10.0f);


    float lastTime = glfwGetTime();
    std::cout << "Main: Entering main loop." << std::endl;    

    while (!glfwWindowShouldClose(window))
    {
        float currentTime = glfwGetTime();
        float deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        checkGLError("glClear");

        float moveSpeed = 10.0f * deltaTime;
        float rotationSpeed = glm::radians(90.0f) * deltaTime;


        float forwardX = sin(characterRotationY);
        float forwardZ = cos(characterRotationY);

        // Movimiento del personaje principal (playerCharacter) - Lógica de movimiento básica
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            characterPosition.x += forwardX * moveSpeed;
            characterPosition.z += forwardZ * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            characterPosition.x -= forwardX * moveSpeed;
            characterPosition.z -= forwardZ * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            characterRotationY += rotationSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            characterRotationY -= rotationSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, true);
        }

        // Después de que characterPosition.x y .z se han actualizado,
        // recalcula la altura Y del terreno en la nueva posición XZ del personaje.
        float currentTerrainHeight = getTerrainHeight(
            characterPosition.x,
            characterPosition.z,
            terrainWidth,          // Variable que define el ancho del terreno
            terrainDepth,          // Variable que define la profundidad del terreno
            terrainBaseY,          // Variable que define el offset Y base
            currentHeightScale,    // ¡IMPORTANTE! Usar la misma currentHeightScale que en el shader
            heightmapCpuData,      // Los datos de la imagen del heightmap
            heightmapWidth,
            heightmapHeight,
            heightmapNrChannels
        );
        
        // El +0.5f (o el valor que uses) es para ajustar si el pivote del modelo no está en sus pies.
        characterPosition.y = currentTerrainHeight + 0.5f;         

        AnimatedModel* playerCharacter = characters[currentCharacterIndex].get(); // El personaje que el jugador controla

        glm::vec3 currentCameraPos = characterPosition + glm::vec3(0.0f, cameraOffset.y, cameraOffset.z);
        glm::mat4 view = glm::lookAt(currentCameraPos,
                                     characterPosition,
                                     glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.5f, 500.0f); 
        checkGLError("Projection Matrix Setup");

        glm::vec3 lightPos = currentCameraPos + glm::vec3(0.0f, 2.0f, -3.0f); // Posición de la luz
        glm::vec3 lightColor = glm::vec3(1.0f, 1.0f, 1.0f); // Color de la luz
        float ambientStrength = 0.5f;
        float diffuseStrength = 0.8f;




        // --- Dibujar el personaje principal (controlable) ---
        glUseProgram(playerCharacter->shaderProgram); 
        checkGLError("glUseProgram for player character");

        glm::mat4 playerModelMat = glm::mat4(1.0f);
        playerModelMat = glm::translate(playerModelMat, characterPosition);
        playerModelMat = glm::rotate(playerModelMat, characterRotationY, glm::vec3(0.0f, 1.0f, 0.0f));
        playerModelMat = glm::scale(playerModelMat, glm::vec3(0.5f)); 
        glUniformMatrix4fv(glGetUniformLocation(playerCharacter->shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(playerModelMat));
        glUniformMatrix4fv(glGetUniformLocation(playerCharacter->shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(playerCharacter->shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        
        glUniform3fv(glGetUniformLocation(playerCharacter->shaderProgram, "lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(glGetUniformLocation(playerCharacter->shaderProgram, "viewPos"), 1, glm::value_ptr(currentCameraPos));
        glUniform3fv(glGetUniformLocation(playerCharacter->shaderProgram, "lightColor"), 1, glm::value_ptr(lightColor));
        glUniform1f(glGetUniformLocation(playerCharacter->shaderProgram, "ambientStrength"), ambientStrength);
        glUniform1f(glGetUniformLocation(playerCharacter->shaderProgram, "diffuseStrength"), diffuseStrength); 
        checkGLError("Uniforms for player character");

        playerCharacter->updateAnimation(deltaTime);
        playerCharacter->Draw(); 
        checkGLError("playerCharacter->Draw()");


        

        // --- Dibujar el suelo ---
        glUseProgram(floorShaderProgram); 
        checkGLError("glUseProgram for floor");
        
        glm::mat4 floorModelMat = glm::mat4(1.0f); 
        glUniformMatrix4fv(glGetUniformLocation(floorShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(floorModelMat));
        glUniformMatrix4fv(glGetUniformLocation(floorShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view)); 
        glUniformMatrix4fv(glGetUniformLocation(floorShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection)); 
        
        // Send lighting uniforms for the floor
        glUniform3fv(glGetUniformLocation(floorShaderProgram, "lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(glGetUniformLocation(floorShaderProgram, "viewPos"), 1, glm::value_ptr(currentCameraPos));
        glUniform3fv(glGetUniformLocation(floorShaderProgram, "lightColor"), 1, glm::value_ptr(lightColor));
        glUniform1f(glGetUniformLocation(floorShaderProgram, "ambientStrength"), ambientStrength);
        glUniform1f(glGetUniformLocation(floorShaderProgram, "diffuseStrength"), diffuseStrength); 
        
        glActiveTexture(GL_TEXTURE0); // Unidad 0 para la textura de hierba
        glBindTexture(GL_TEXTURE_2D, floorTextureID);
        glUniform1i(glGetUniformLocation(floorShaderProgram, "ourTexture"), 0); 

        glActiveTexture(GL_TEXTURE1); // Unidad 1 para la textura del heightmap
        glBindTexture(GL_TEXTURE_2D, heightmapTextureID);
        glUniform1i(glGetUniformLocation(floorShaderProgram, "heightmap"), 1);


        glActiveTexture(GL_TEXTURE2); // Unidad 2 para la arena
        glBindTexture(GL_TEXTURE_2D, sandTextureID);
        glUniform1i(glGetUniformLocation(floorShaderProgram, "sandTexture"), 2);

        glActiveTexture(GL_TEXTURE3); // Unidad 3 para la roca
        glBindTexture(GL_TEXTURE_2D, rockTextureID);
        glUniform1i(glGetUniformLocation(floorShaderProgram, "rockTexture"), 3);
        glUniform1f(glGetUniformLocation(floorShaderProgram, "sampleDist"), heightmapPixelSize);

        glActiveTexture(GL_TEXTURE4); // Unidad 4 para la nieve
        glBindTexture(GL_TEXTURE_2D, snowTextureID);
        glUniform1i(glGetUniformLocation(floorShaderProgram, "snowTexture"), 4);

        // Uniforms de escala para el heightmap y repetición de hierba
        glUniform1f(glGetUniformLocation(floorShaderProgram, "heightScale"),  currentHeightScale);//;40.0f); // Puedes ajustar este valor
        glUniform1f(glGetUniformLocation(floorShaderProgram, "terrainYOffset"), -16.01f);
        glUniform2f(glGetUniformLocation(floorShaderProgram, "grassTexRepeat"), 24.0f, 24.0f); // Ajustado para un terreno 100x100
        checkGLError("Uniforms for floor");

        glBindVertexArray(floorVAO);
        checkGLError("glBindVertexArray for floor draw");

        // Dibujar con glDrawElements en lugar de glDrawArrays ---
        glDrawElements(GL_TRIANGLES, floorIndicesVec.size(), GL_UNSIGNED_INT, 0);
        checkGLError("glDrawElements for floor");
        
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0); 
        checkGLError("Unbind VAO/Texture for floor");



//-------NEWOBJ-----
///*
        // --- Posición del nuevo objeto PNG ---
        glm::vec3 objectPosXZ = glm::vec3(10.0f, 0.0f, 10.0f); // Posición X y Z deseadas
        // Usa getTerrainHeight para obtener la altura Y en esa posición
  
        float objectY = getTerrainHeight(
            objectPosXZ.x,
            objectPosXZ.z,
            terrainWidth,          // Variable que define el ancho del terreno
            terrainDepth,          // Variable que define la profundidad del terreno
            terrainBaseY,          // Variable que define el offset Y base
            currentHeightScale,    // ¡IMPORTANTE! Usar la misma currentHeightScale que en el shader
            heightmapCpuData,      // Los datos de la imagen del heightmap
            heightmapWidth,
            heightmapHeight,
            heightmapNrChannels
        );
        
        // Añade un pequeño offset para que no quede enterrado o "flote" justo en la superficie
        objectY += 0.01f; // Ajusta este valor si el objeto se ve hundido o flotando

        // --- Dibujar el nuevo objeto PNG ---
        glUseProgram(objectShaderProgram); // Activa el shader del objeto
        checkGLError("glUseProgram for new object");

        glm::mat4 objectModelMat = glm::mat4(1.0f);
        objectModelMat = glm::translate(objectModelMat, glm::vec3(objectPosXZ.x, objectY, objectPosXZ.z));
        objectModelMat = glm::rotate(objectModelMat, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)); // Si es un quad vertical
        objectModelMat = glm::scale(objectModelMat, glm::vec3(5.0f)); // Ajusta el tamaño del PNG

        glUniformMatrix4fv(glGetUniformLocation(objectShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(objectModelMat));
        glUniformMatrix4fv(glGetUniformLocation(objectShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(objectShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        
        glActiveTexture(GL_TEXTURE0); // Usar la unidad de textura 0 para el objeto
        glBindTexture(GL_TEXTURE_2D, newObjectTextureID);
        glUniform1i(glGetUniformLocation(objectShaderProgram, "ourTexture"), 0);

        glBindVertexArray(newObjectVAO);
        // Usar glDrawArrays para un quad de 4 vértices sin EBO
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4); // O GL_TRIANGLES si usaste índices para el quad
        // Si creaste un EBO para el quad: glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        checkGLError("glDrawArrays for new object");
        
        glBindVertexArray(0); // Desenlazar VAO
        glBindTexture(GL_TEXTURE_2D, 0); // Desenlazar textura

//*/





        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    std::cout << "Main: Exiting main loop." << std::endl;

    // --- Free floor resources ---
    glDeleteProgram(floorShaderProgram);
    glDeleteVertexArrays(1, &floorVAO);
    glDeleteBuffers(1, &floorVBO);
    glDeleteBuffers(1, &floorEBO);
    glDeleteTextures(1, &floorTextureID); 
    glDeleteTextures(1, &sandTextureID); 
    glDeleteTextures(1, &rockTextureID); 
    glDeleteTextures(1, &heightmapTextureID);
    checkGLError("Freeing floor resources");

    if (heightmapCpuData) {
        stbi_image_free(heightmapCpuData);
        std::cout << "Heightmap CPU data freed." << std::endl;
    }


//-------NEWOBJ-----
    glDeleteProgram(objectShaderProgram);
    glDeleteVertexArrays(1, &newObjectVAO);
    glDeleteBuffers(1, &newObjectVBO);
    glDeleteTextures(1, &newObjectTextureID);



    glfwTerminate();
    std::cout << "Main: GLFW terminated." << std::endl;
    return 0;
}



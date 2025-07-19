#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <atomic>

// Animation Process
constexpr const bool animating = true;

// Window settings
constexpr int WINDOW_WIDTH = 192 * 7;
constexpr int WINDOW_HEIGHT = 108 * 7;
constexpr char WINDOW_TITLE[] = "Fractal Renderer";

// Mandelbrot parameters
constexpr double ESCAPE_RADIUS_SQUARED = 100.0 * 100.0;
constexpr double ASPECT_RATIO = static_cast<double>(WINDOW_WIDTH) / WINDOW_HEIGHT;

// Performance settings
const int NUM_THREADS = std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 8;
constexpr float SCROLL_RENDER_DELAY = 0.1f;

// Anti-aliasing settings
constexpr int AA_MAX_SAMPLES = 6; // 4x4 = 16 samples per pixel at maximum

// Rendering state
struct RenderState {
    double viewportX = -0.5;
    double viewportY = 0;
    double viewportHeight = 3.0;
    int maxIterations = 128;
    float colorDensity = 0.2;
    bool showJulia = false;
    double juliaX = -0.8;
    double juliaY = 0.156;
    int colorScheme = 0;
    bool autoIterations = true;
    int fractalType = 0;
    bool stripes = false;
    float stripeFrequency = 5;
    float stripeIntensity = 10;
    bool innerCalculation = false;
    bool antiAliasing = false;

    // Helper to get the viewport width based on aspect ratio
    double getViewportWidth() const {
        return viewportHeight * ASPECT_RATIO;
    }
};

struct ReturnInfo {
    int iteration;
    double smoothIteration;
    double stripeSum;
};

// Forward declarations
void renderFractalRegion(sf::Uint8* pixels, const RenderState& state, int startY, int endY, int width, int height);
void saveScreenshot(const sf::Texture& texture, const RenderState& state);
std::string getInfoString(const RenderState& state, double mouseX, double mouseY);

// Color palettes
const std::vector<std::vector<sf::Color>> PALETTES = {
    {
        sf::Color(66, 30, 15), sf::Color(25, 7, 26), sf::Color(9, 1, 47),
        sf::Color(4, 4, 73), sf::Color(0, 7, 100), sf::Color(12, 44, 138),
        sf::Color(24, 82, 177), sf::Color(57, 125, 209), sf::Color(134, 181, 229),
        sf::Color(211, 236, 248), sf::Color(241, 233, 191), sf::Color(248, 201, 95),
        sf::Color(255, 170, 0), sf::Color(204, 128, 0), sf::Color(153, 87, 0),
    },
    {
        sf::Color(0, 0, 0), sf::Color(255, 255, 255),
    }
};

// Linear interpolation for colors
inline sf::Color interpolateColors(const sf::Color& c1, const sf::Color& c2, double factor) {
    return sf::Color(
        static_cast<sf::Uint8>(c1.r + factor * (c2.r - c1.r)),
        static_cast<sf::Uint8>(c1.g + factor * (c2.g - c1.g)),
        static_cast<sf::Uint8>(c1.b + factor * (c2.b - c1.b))
    );
}

// Fast calculation of fractal iteration count with optimizations
inline ReturnInfo calculateFractal(double cr, double ci, double jr, double ji, int maxIter, bool isJulia, int fractalType, bool stripes, float stripeFrequency, bool innerCalculation) {
    // Set initial values based on fractal type
    double zr = isJulia ? cr : 0;
    double zi = isJulia ? ci : 0;
    double cr_actual = isJulia ? jr : cr;
    double ci_actual = isJulia ? ji : ci;

    ReturnInfo iterationInfo;

    // Early bailout checks for Mandelbrot
    if (!innerCalculation && !isJulia && fractalType == 0) {
        // Cardioid check
        double q = (cr - 0.25) * (cr - 0.25) + ci * ci;
        if (q * (q + (cr - 0.25)) < 0.25 * ci * ci) {
            iterationInfo.iteration = -1;
            return iterationInfo;
        }

        // Period-2 bulb check
        if ((cr + 1.0) * (cr + 1.0) + ci * ci < 0.0625) {
            iterationInfo.iteration = -1;
            return iterationInfo;
        }
    }

    double zr2 = zr * zr;
    double zi2 = zi * zi;
    float stripeSum = 0;
    int i = 0;

    // Main iteration loop (optimized)
    while (zr2 + zi2 < ESCAPE_RADIUS_SQUARED) {
        zi = (fractalType == 0) ? 2 * zr * zi : 2 * fabs(zr * zi);
        zi += ci_actual;
        zr = zr2 - zi2 + cr_actual;
        zr2 = zr * zr;
        zi2 = zi * zi;
        if (stripes) stripeSum += powf(sin(atan2(zi, zr) * stripeFrequency), 2.0);
        i++;
        if (i == maxIter) {
            if (innerCalculation) {
                iterationInfo.iteration = i;
                iterationInfo.smoothIteration = i + 1 - log(log(zr2 + zi2) / 2) / log(2);
                iterationInfo.stripeSum = stripeSum;
                return iterationInfo;
            }
            else {
                iterationInfo.iteration = -1;
                return iterationInfo;
            }
        }
    }

    // Smooth coloring formula
    iterationInfo.iteration = i;
    iterationInfo.smoothIteration = i + 1 - log(log(zr2 + zi2) / 2) / log(2);
    iterationInfo.stripeSum = stripeSum;
    return iterationInfo;
}

// Calculate anti-aliased pixel color by sampling multiple points
sf::Color calculateAntiAliasedColor(int x, int y, const RenderState& state, int width, int height, const std::vector<sf::Color>& palette) {
    double pixelHeight = state.viewportHeight / height;
    double pixelWidth = state.getViewportWidth() / width;
    double halfHeight = state.viewportHeight / 2;
    double halfWidth = state.getViewportWidth() / 2;

    int samples = AA_MAX_SAMPLES + 1; // 1 = 2x2, 2 = 3x3, 3 = 4x4

    // Sample grid
    std::vector<sf::Color> sampleColors;
    sampleColors.reserve(samples * samples);

    for (int sy = 0; sy < samples; sy++) {
        for (int sx = 0; sx < samples; sx++) {
            // Calculate subpixel position
            double offsetX = (sx + 0.5) / samples;
            double offsetY = (sy + 0.5) / samples;

            double cr = state.viewportX - halfWidth + (x + offsetX) * pixelWidth;
            double ci = state.viewportY - halfHeight + (y + offsetY) * pixelHeight;

            // Calculate iterations for this sample
            ReturnInfo info = calculateFractal(cr, ci, state.juliaX, state.juliaY,
                state.maxIterations, state.showJulia, state.fractalType, state.stripes,
                state.stripeFrequency, state.innerCalculation);

            sf::Color color;
            if (info.iteration == -1) {
                color = sf::Color(0, 0, 0);
            }
            else {
                float iterations;
                if (state.stripes) {
                    iterations = state.stripeIntensity * (info.stripeSum / info.iteration);
                }
                else {
                    iterations = info.smoothIteration * state.colorDensity;
                }
                int index = static_cast<int>(iterations) % palette.size();
                double fract = iterations - std::floor(iterations);
                color = interpolateColors(palette[index], palette[(index + 1) % palette.size()], fract);
            }

            sampleColors.push_back(color);
        }
    }

    // Average the samples
    int totalR = 0, totalG = 0, totalB = 0;
    for (const auto& color : sampleColors) {
        totalR += color.r;
        totalG += color.g;
        totalB += color.b;
    }

    int sampleCount = sampleColors.size();
    return sf::Color(
        static_cast<sf::Uint8>(totalR / sampleCount),
        static_cast<sf::Uint8>(totalG / sampleCount),
        static_cast<sf::Uint8>(totalB / sampleCount)
    );
}

// Render the fractal using multiple threads
void renderFractal(sf::Uint8* pixels, const RenderState& state, int width, int height, bool usePreview = false) {
    std::vector<std::thread> threads;
    int linesPerThread = height / NUM_THREADS;

    for (int i = 0; i < NUM_THREADS; i++) {
        int startY = i * linesPerThread;
        int endY = (i == NUM_THREADS - 1) ? height : (i + 1) * linesPerThread;
        threads.emplace_back(renderFractalRegion, pixels, std::ref(state), startY, endY, width, height);
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

// Render a region of the fractal (for multi-threading)
void renderFractalRegion(sf::Uint8* pixels, const RenderState& state, int startY, int endY, int width, int height) {
    double pixelHeight = state.viewportHeight / height;
    double pixelWidth = state.getViewportWidth() / width;
    double halfHeight = state.viewportHeight / 2;
    double halfWidth = state.getViewportWidth() / 2;
    const auto& palette = PALETTES[state.colorScheme % PALETTES.size()];

    for (int y = startY; y < endY; y++) {
        for (int x = 0; x < width; x++) {
            sf::Color color;

            // Use anti-aliasing if enabled
            if (state.antiAliasing) {
                color = calculateAntiAliasedColor(x, y, state, width, height, palette);
            }
            else {
                // Standard single-sample rendering
                double cr = state.viewportX - halfWidth + x * pixelWidth;
                double ci = state.viewportY - halfHeight + y * pixelHeight;

                // Calculate iterations
                ReturnInfo info = calculateFractal(cr, ci, state.juliaX, state.juliaY,
                    state.maxIterations, state.showJulia, state.fractalType, state.stripes, state.stripeFrequency, state.innerCalculation);

                if (info.iteration == -1) {
                    color = sf::Color(0, 0, 0);
                }
                else {
                    float iterations;
                    if (state.stripes) {
                        iterations = state.stripeIntensity * (info.stripeSum / info.iteration);
                    }
                    else {
                        iterations = info.smoothIteration * state.colorDensity;
                    }
                    int index = static_cast<int>(iterations) % palette.size();
                    double fract = iterations - std::floor(iterations);
                    color = interpolateColors(palette[index], palette[(index + 1) % palette.size()], fract);
                }
            }

            int pixelIndex = (y * width + x) * 4;
            pixels[pixelIndex] = color.r;
            pixels[pixelIndex + 1] = color.g;
            pixels[pixelIndex + 2] = color.b;
            pixels[pixelIndex + 3] = 255;
        }
    }
}

// Save screenshot with location info in filename
void saveScreenshot(const sf::Texture& texture, const RenderState& state) {
    sf::Image screenshot = texture.copyToImage();

    time_t now = time(0);
    tm timeinfo;

#ifdef _WIN32
    localtime_s(&timeinfo, &now);
#else
    tm* temp = localtime(&now);
    if (temp) timeinfo = *temp;
#endif

    std::stringstream filename;
    filename << "fractal_" << (state.showJulia ? "julia_" : "mandelbrot_")
        << std::fixed << std::setprecision(6) << state.viewportX << "_" << state.viewportY
        << "_zoom_" << std::setprecision(2) << (3.0 / state.viewportX)
        << now << ".png";

    screenshot.saveToFile(filename.str());
    std::cout << "Screenshot saved: " << filename.str() << std::endl;
}


// Auto-adjust iterations based on zoom level
void adjustIterations(RenderState& state) {
    if (state.autoIterations) {
        double zoomFactor = 3.0 / state.viewportHeight;
        state.maxIterations = std::min(10000, static_cast<int>(100 * log10(1 + zoomFactor)));
        state.maxIterations = std::max(100, state.maxIterations);
    }
}

int main() {
    std::cout << "Starting Fractal Explorer with " << NUM_THREADS << " threads" << std::endl;

    // Create window and rendering resources
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), WINDOW_TITLE);
    window.setFramerateLimit(60);

    sf::Texture texture;
    if (!texture.create(WINDOW_WIDTH, WINDOW_HEIGHT)) {
        std::cerr << "Failed to create texture" << std::endl;
        return 1;
    }

    sf::Sprite sprite(texture);
    sf::Uint8* pixels = new sf::Uint8[WINDOW_WIDTH * WINDOW_HEIGHT * 4];

    // Load font for text display
    sf::Font font;
    bool hasFontLoaded = font.loadFromFile("arial.ttf");
    if (!hasFontLoaded) {
        // Try system fonts as fallback
#ifdef _WIN32
        hasFontLoaded = font.loadFromFile("C:\\Windows\\Fonts\\arial.ttf");
#elif __APPLE__
        hasFontLoaded = font.loadFromFile("/System/Library/Fonts/Helvetica.ttc");
#elif __linux__
        hasFontLoaded = font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
#endif
    }

    // Initialize state and render
    RenderState state;
    adjustIterations(state);

    auto startTime = std::chrono::high_resolution_clock::now();
    renderFractal(pixels, state, WINDOW_WIDTH, WINDOW_HEIGHT);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    std::cout << "Initial render: " << duration << "ms" << std::endl;
    texture.update(pixels);

    // Tracking variables
    sf::Vector2i lastMousePos;
    bool isDragging = false;
    std::string renderTimeStr = "Render time: " + std::to_string(duration) + "ms";
    sf::Vector2i currentMousePos;
    double mouseComplexX = 0, mouseComplexY = 0;

    // High-quality render control
    bool viewChanged = false;
    sf::Clock scrollTimer;
    bool pendingHighQualityRender = false;

    // Main loop
    while (window.isOpen()) {
        sf::Event event;
        bool needsRedraw = false;
        bool usePreview = false;

        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            if (animating) {

                break; }

            if (event.type == sf::Event::MouseWheelScrolled) {
                sf::Vector2i mousePos = sf::Mouse::getPosition(window);

                if (mousePos.x >= 0 && mousePos.x < WINDOW_WIDTH &&
                    mousePos.y >= 0 && mousePos.y < WINDOW_HEIGHT) {

                    double halfWidth = state.getViewportWidth() / 2;
                    double halfHeight = state.viewportHeight / 2;
                    double mouseX = state.viewportX - halfWidth + mousePos.x * state.getViewportWidth() / WINDOW_WIDTH;
                    double mouseY = state.viewportY - halfHeight + mousePos.y * state.viewportHeight / WINDOW_HEIGHT;

                    double zoomFactor = (event.mouseWheelScroll.delta > 0) ? 0.5 : 2.0;

                    state.viewportX = mouseX + (state.viewportX - mouseX) * zoomFactor;
                    state.viewportY = mouseY + (state.viewportY - mouseY) * zoomFactor;
                    state.viewportHeight *= zoomFactor;

                    adjustIterations(state);

                    needsRedraw = true;
                    usePreview = true;
                    viewChanged = true;
                    scrollTimer.restart();
                }
            }

            // Handle mouse button events for dragging
            if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                isDragging = true;
                usePreview = true;
                lastMousePos = sf::Mouse::getPosition(window);
            }

            if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {
                isDragging = false;
                pendingHighQualityRender = true;
                scrollTimer.restart();
            }

            // Handle mouse movement for panning and tracking
            if (event.type == sf::Event::MouseMoved) {
                currentMousePos = sf::Mouse::getPosition(window);

                double halfWidth = state.getViewportWidth() / 2;
                double halfHeight = state.viewportHeight / 2;
                mouseComplexX = state.viewportX - halfWidth +
                    currentMousePos.x * state.getViewportWidth() / WINDOW_WIDTH;
                mouseComplexY = state.viewportY - halfHeight +
                    currentMousePos.y * state.viewportHeight / WINDOW_HEIGHT;

                if (isDragging) {
                    sf::Vector2i delta = lastMousePos - currentMousePos;

                    double deltaX = delta.x * state.getViewportWidth() / WINDOW_WIDTH;
                    double deltaY = delta.y * state.viewportHeight / WINDOW_HEIGHT;

                    state.viewportX += deltaX;
                    state.viewportY += deltaY;

                    lastMousePos = currentMousePos;
                    needsRedraw = true;
                    usePreview = true;
                    viewChanged = true;
                    scrollTimer.restart();
                }
            }

            // Key press events
            if (event.type == sf::Event::KeyPressed) {
                switch (event.key.code) {
                case sf::Keyboard::R: // Reset view
                    state.viewportX = -0.5;
                    state.viewportY = 0.0;
                    state.viewportHeight = 3.0;
                    adjustIterations(state);
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = true;
                    break;
                case sf::Keyboard::J: // Toggle Julia/Mandelbrot
                    state.showJulia = !state.showJulia;
                    if (!state.showJulia) {
                        // When switching back to Mandelbrot, reset Julia seed to mouse position
                        state.juliaX = mouseComplexX;
                        state.juliaY = mouseComplexY;
                    }
                    needsRedraw = true;
                    break;
                case sf::Keyboard::C: // Change color scheme
                    state.colorScheme = (state.colorScheme + 1) % PALETTES.size();
                    needsRedraw = true;
                    break;
                case sf::Keyboard::I: // Increase iterations
                    state.maxIterations = static_cast<int>(state.maxIterations * 1.5);
                    state.autoIterations = false;
                    needsRedraw = true;
                    break;
                case sf::Keyboard::K: // Decrease iterations
                    state.maxIterations = std::max(50, static_cast<int>(state.maxIterations / 1.5));
                    state.autoIterations = false;
                    needsRedraw = true;
                    break;
                case sf::Keyboard::A: // Toggle auto iterations
                    state.autoIterations = !state.autoIterations;
                    if (state.autoIterations) {
                        adjustIterations(state);
                        needsRedraw = true;
                    }
                    break;
                case sf::Keyboard::T: // Toggle fractal type
                    state.fractalType = (state.fractalType + 1) % 2; // Currently 2 types (0=Mandelbrot, 1=Burning Ship)
                    needsRedraw = true;
                    break;
                case sf::Keyboard::B: // Toggle stripe effect
                    state.stripes = !state.stripes;
                    needsRedraw = true;
                    break;
                case sf::Keyboard::F: // Toggle anti-aliasing
                    state.antiAliasing = !state.antiAliasing;
                    needsRedraw = true;
                    pendingHighQualityRender = true;
                    break;
                case sf::Keyboard::N: // Toggle inner calculation
                    state.innerCalculation = !state.innerCalculation;
                    needsRedraw = true;
                    break;
                case sf::Keyboard::Up: // Increase color density
                    state.colorDensity *= 1.2f;
                    needsRedraw = true;
                    break;
                case sf::Keyboard::Down: // Decrease color density
                    state.colorDensity /= 1.2f;
                    needsRedraw = true;
                    break;
                case sf::Keyboard::Left: // Decrease stripe frequency
                    if (state.stripes) {
                        state.stripeFrequency = std::max(1.0f, state.stripeFrequency - 1.0f);
                        needsRedraw = true;
                    }
                    break;
                case sf::Keyboard::Right: // Increase stripe frequency
                    if (state.stripes) {
                        state.stripeFrequency += 1.0f;
                        needsRedraw = true;
                    }
                    break;
                }
            }
        }



        // Check if we need to render a high-quality image after scrolling/panning stops
        if (viewChanged && scrollTimer.getElapsedTime().asSeconds() > SCROLL_RENDER_DELAY) {
            pendingHighQualityRender = true;
            viewChanged = false;
        }

        // Perform high-quality render if needed
        if (pendingHighQualityRender) {
            startTime = std::chrono::high_resolution_clock::now();
            renderFractal(pixels, state, WINDOW_WIDTH, WINDOW_HEIGHT, false);
            endTime = std::chrono::high_resolution_clock::now();
            duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            renderTimeStr = "Render time: " + std::to_string(duration) + "ms";
            texture.update(pixels);
            pendingHighQualityRender = false;
        }
        else if (needsRedraw) {
            // Use low-quality preview for interactive movements
            startTime = std::chrono::high_resolution_clock::now();
            renderFractal(pixels, state, WINDOW_WIDTH, WINDOW_HEIGHT, usePreview);
            endTime = std::chrono::high_resolution_clock::now();
            duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            renderTimeStr = usePreview ? "Preview time: " + std::to_string(duration) + "ms"
                : "Render time: " + std::to_string(duration) + "ms";
            texture.update(pixels);
        }

        // Draw everything
        window.clear();
        window.draw(sprite);
        window.display();
        if (animating) {
            saveScreenshot(texture, state);
            viewChanged = true;
            state.viewportX += (-1.61299 - state.viewportX) / 25;
            state.viewportY += (-1.4822e-05 - state.viewportY) / 25;
            state.viewportHeight += (3.41061e-13 - state.viewportHeight) / 25;
            state.colorDensity += (0.0186928 - state.colorDensity) / 25;
            state.maxIterations += (1941 - state.maxIterations) / 25;
        }
    }

    // Clean up
    delete[] pixels;

    return 0;
}

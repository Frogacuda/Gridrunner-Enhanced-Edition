// Store radiance/coverage rather than resolved color so overlaps still combine.
// Planes that do not fit keep the same Arms shading and draw directly.
// Included once by beam_renderer.cpp, inside namespace grpp.
// Bake with the accepted Arms shader; never rebuild a CPU per-cell beam mesh.
void BeamRenderer::drawAtlas(const std::vector<GridPlane> &planes, float sx, float sy, float w,
                             int width, int height, int x, int y) {
    constexpr int Size = 1024, Pad = 2;
    struct Rect {
        float x0, y0, x1, y1;
    };
    struct Slot {
        int plane = 0, x = 0, y = 0, width = 0, height = 0;
        float loX = 0, loY = 0, hiX = 0, hiY = 0;
        double merit = 0;
        bool placed = false;
        std::vector<Rect> rects;
    };
    std::vector<Slot> slots;
    // Supersample narrow cores without changing any physical-pixel kernel constants.
    const float scale = w <= 3 ? 2.f : 1.f;
    for (size_t pi = 0; pi < planes.size(); ++pi) {
        const auto &p = planes[pi];
        if (!packOriginalColor(p.color) || p.energy <= 0 || p.lastX < p.firstX ||
            p.lastY < p.firstY)
            continue;
        const int n = p.shades, rows = p.mode == 3 ? 2 : 1;
        const float reach = std::min(64.f, std::max(2.f, w * (p.mode == 1 ? 3 : 4) * 2));
        std::vector<Rect> arms;
        auto add = [&](double ax, double ay, double bx, double by) {
            arms.push_back(
                {float(std::min(ax, bx) * sx) - reach, float(std::min(ay, by) * sy) - reach,
                 float(std::max(ax, bx) * sx) + reach, float(std::max(ay, by) * sy) + reach});
        };
        if (p.mode == 0) {
            add(-p.rx, 0, p.rx, 0);
            add(0, -p.ry, 0, p.ry);
        } else if (p.mode == 1) {
            for (int i = 0; i < n; ++i) {
                double xx = p.rx - i * p.rx / n, yy = p.ry - i * p.ry / n;
                add(-xx, 0, -xx, 0);
                add(xx, 0, xx, 0);
                add(0, -yy, 0, -yy);
                add(0, yy, 0, yy);
            }
        } else {
            for (int row = 0; row < rows; ++row) {
                double yy = p.mode == 3 ? (row == 0 ? -p.ry : p.ry) : 0;
                add(-p.rx, yy, 0, yy);
                add(p.rx, yy, 0, yy);
            }
            if (std::abs(p.rx - p.ry) < 1.e-8) {
                for (int row = 0; row < rows; ++row) {
                    double xx = p.mode == 3 ? (row == 0 ? -p.rx : p.rx) : 0;
                    add(xx, -p.ry, xx, 0);
                    add(xx, p.ry, xx, 0);
                }
            } else
                for (int i = 0; i < n; ++i)
                    for (int row = 0; row < rows; ++row) {
                        double xx = p.mode == 3 ? (row == 0 ? -p.rx : p.rx) : 0,
                               top = -p.ry + i * p.rx / n, bottom = p.ry - p.ry / n - i * p.rx / n;
                        add(xx, top, xx, top + p.ry / n);
                        add(xx, bottom, xx, bottom + p.ry / n);
                    }
        }
        Slot slot;
        slot.plane = int(pi);
        slot.loX = slot.loY = 1.e30f;
        slot.hiX = slot.hiY = -1.e30f;
        double fill = 0;
        for (auto r : arms) {
            slot.loX = std::min(slot.loX, r.x0);
            slot.loY = std::min(slot.loY, r.y0);
            slot.hiX = std::max(slot.hiX, r.x1);
            slot.hiY = std::max(slot.hiY, r.y1);
            fill += (r.x1 - r.x0) * (r.y1 - r.y0);
        }
        slot.width = int(std::ceil((slot.hiX - slot.loX) * scale)) + 2 * Pad;
        slot.height = int(std::ceil((slot.hiY - slot.loY) * scale)) + 2 * Pad;
        if (slot.width > Size || slot.height > Size)
            continue;
        int visible = 0;
        for (int ix = p.firstX; ix <= p.lastX; ++ix)
            for (int iy = p.firstY; iy <= p.lastY; ++iy) {
                float cx = float((p.x + ix * p.spacing + .5) * sx),
                      cy = float((p.y + iy * p.spacing + .5) * sy);
                if (cx + slot.hiX >= 0 && cx + slot.loX <= width && cy + slot.hiY >= 0 &&
                    cy + slot.loY <= height)
                    ++visible;
            }
        if (visible < 2)
            continue;
        const float tile = std::max(4.f, 4 * w);
        const int nx = int(std::ceil((slot.hiX - slot.loX) / tile)),
                  ny = int(std::ceil((slot.hiY - slot.loY) / tile));
        std::vector<unsigned char> used(size_t(nx) * ny);
        for (auto r : arms) {
            int ax = std::clamp(int(std::floor((r.x0 - slot.loX) / tile)), 0, nx - 1),
                bx = std::clamp(int(std::floor((r.x1 - slot.loX) / tile)), 0, nx - 1),
                ay = std::clamp(int(std::floor((r.y0 - slot.loY) / tile)), 0, ny - 1),
                by = std::clamp(int(std::floor((r.y1 - slot.loY) / tile)), 0, ny - 1);
            for (int yy = ay; yy <= by; ++yy)
                for (int xx = ax; xx <= bx; ++xx)
                    used[size_t(yy) * nx + xx] = 1;
        }
        // Merge identical occupied row runs; rectangles remain disjoint.
        struct Run {
            int left, right, top;
        };
        std::vector<Run> active;
        auto close = [&](Run r, int bottom) {
            slot.rects.push_back({slot.loX + r.left * tile, slot.loY + r.top * tile,
                                  std::min(slot.hiX, slot.loX + r.right * tile),
                                  std::min(slot.hiY, slot.loY + bottom * tile)});
        };
        for (int yy = 0; yy <= ny; ++yy) {
            std::vector<Run> next;
            if (yy < ny)
                for (int xx = 0; xx < nx;) {
                    if (!used[size_t(yy) * nx + xx]) {
                        ++xx;
                        continue;
                    }
                    int right = xx;
                    while (right < nx && used[size_t(yy) * nx + right])
                        ++right;
                    Run run{xx, right, yy};
                    for (auto a : active)
                        if (a.left == xx && a.right == right)
                            run.top = a.top;
                    next.push_back(run);
                    xx = right;
                }
            for (auto a : active) {
                bool continued = false;
                for (auto b : next)
                    if (a.left == b.left && a.right == b.right) {
                        continued = true;
                        break;
                    }
                if (!continued)
                    close(a, yy);
            }
            active = std::move(next);
        }
        double stampFill = 0;
        for (auto r : slot.rects)
            stampFill += (r.x1 - r.x0) * (r.y1 - r.y0);
        const double directCost = visible * fill,
                     bakeCost = scale * scale * fill + visible * stampFill * .35;
        if (bakeCost >= directCost * .8)
            continue;
        slot.merit = (directCost - bakeCost) / (slot.width * slot.height);
        slots.push_back(std::move(slot));
    }
    // Greedy bounded shelf allocation; unselected planes remain ordinary GPU Arms.
    std::vector<size_t> order(slots.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return slots[a].merit > slots[b].merit; });
    int cursorX = 0, cursorY = 0, rowHeight = 0;
    for (auto i : order) {
        auto &s = slots[i];
        int nx = cursorX, ny = cursorY, rh = rowHeight;
        if (nx + s.width > Size) {
            nx = 0;
            ny += rh;
            rh = 0;
        }
        if (ny + s.height > Size)
            continue;
        s.x = nx;
        s.y = ny;
        s.placed = true;
        cursorX = nx + s.width;
        cursorY = ny;
        rowHeight = std::max(rh, s.height);
    }
    std::vector<int> selected(planes.size(), -1);
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i].placed) {
            selected[size_t(slots[i].plane)] = int(i);
            ++atlasStats_.bakedPlanes;
        }
    if (!atlasStats_.bakedPlanes) {
        atlasStats_.directPlanes = int(planes.size());
        drawGridPlanes(planes, sx, sy, w, width, height, x, y);
        return;
    }
    if (!stampProgram_) {
        if (!glad_glVertexAttribDivisor)
            glad_glVertexAttribDivisor = reinterpret_cast<PFNGLVERTEXATTRIBDIVISORPROC>(
                SDL_GL_GetProcAddress("glVertexAttribDivisor"));
        if (!glad_glVertexAttribDivisor)
            throw std::runtime_error("GLES instance attributes unavailable");
        const char *vertex = R"(#version 300 es
precision highp float;
layout(location=0) in vec4 rect;layout(location=1) in vec4 atlasRect;
uniform vec2 viewportSize;uniform vec2 atlasSize;out vec2 uv;
void main(){vec2 q=vec2(float(gl_VertexID&1),float(gl_VertexID>>1));vec2 p=mix(rect.xy,rect.zw,q),a=mix(atlasRect.xy,atlasRect.zw,q);uv=vec2(a.x/atlasSize.x,1.-a.y/atlasSize.y);gl_Position=vec4(p*vec2(2.,-2.)/viewportSize+vec2(-1.,1.),0,1);})";
        const char *fragment = R"(#version 300 es
precision highp float;in vec2 uv;uniform sampler2D radianceAtlas;uniform sampler2D coverageAtlas;
layout(location=0) out vec4 colour;layout(location=1) out vec4 coverage;
void main(){colour=vec4(texture(radianceAtlas,uv).rgb,0);coverage=vec4(texture(coverageAtlas,uv).r,0,0,0);})";
        unsigned v = compileBeam(GL_VERTEX_SHADER, vertex),
                 f = compileBeam(GL_FRAGMENT_SHADER, fragment);
        stampProgram_ = glCreateProgram();
        glAttachShader(stampProgram_, v);
        glAttachShader(stampProgram_, f);
        glLinkProgram(stampProgram_);
        glDeleteShader(v);
        glDeleteShader(f);
        int linked = 0;
        glGetProgramiv(stampProgram_, GL_LINK_STATUS, &linked);
        if (!linked)
            throw std::runtime_error("Atlas stamp link failed");
        stampUniforms_.load(stampProgram_);
        glGenVertexArrays(1, &stampVao_);
        glBindVertexArray(stampVao_);
        glGenBuffers(1, &stampBuffer_);
        glBindBuffer(GL_ARRAY_BUFFER, stampBuffer_);
        for (unsigned i = 0; i < 2; ++i) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(Stamp),
                                  reinterpret_cast<void *>(size_t(i) * 4 * sizeof(float)));
            glVertexAttribDivisor(i, 1);
        }
    }
    GLint target = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!atlasFramebuffer_) {
        glGenFramebuffers(1, &atlasFramebuffer_);
        glBindFramebuffer(GL_FRAMEBUFFER, atlasFramebuffer_);
        glGenTextures(1, &atlasLight_);
        glGenTextures(1, &atlasCoverage_);
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, i ? atlasCoverage_ : atlasLight_);
            glTexImage2D(GL_TEXTURE_2D, 0, i ? GL_R8 : GL_RGB10_A2, Size, Size, 0,
                         i ? GL_RED : GL_RGBA,
                         i ? GL_UNSIGNED_BYTE : GL_UNSIGNED_INT_2_10_10_10_REV, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D,
                                   i ? atlasCoverage_ : atlasLight_, 0);
        }
        const GLenum attachments[]{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, attachments);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Atlas framebuffer incomplete");
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, atlasFramebuffer_);
    glDisable(GL_SCISSOR_TEST);
    const GLfloat zero[]{0, 0, 0, 0};
    glClearBufferfv(GL_COLOR, 0, zero);
    glClearBufferfv(GL_COLOR, 1, zero);
    glEnable(GL_SCISSOR_TEST);
    for (const auto &slot : slots)
        if (slot.placed) {
            auto p = planes[size_t(slot.plane)];
            p.x = (slot.x + Pad - slot.loX * scale) / (sx * scale) - .5;
            p.y = (slot.y + Pad - slot.loY * scale) / (sy * scale) - .5;
            p.firstX = p.lastX = p.firstY = p.lastY = 0;
            drawGridPlanes({p}, sx * scale, sy * scale, w * scale, Size, Size, 0, 0, scale);
        }
    glBindFramebuffer(GL_FRAMEBUFFER, unsigned(target));
    glViewport(x, y, width, height);
    glScissor(x, y, width, height);
    atlasStats_.bytes = uint64_t(Size) * Size * 5;
    stamps_.clear();
    std::vector<GridPlane> direct;
    auto flushStamps = [&]() {
        if (stamps_.empty())
            return;
        glUseProgram(stampProgram_);
        glBindVertexArray(stampVao_);
        glBindBuffer(GL_ARRAY_BUFFER, stampBuffer_);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(stamps_.size() * sizeof(Stamp)), stamps_.data(),
                     GL_STREAM_DRAW);
        glUniform2f(stampUniforms_.get(Uniform::ViewportSize), float(width), float(height));
        glUniform2f(stampUniforms_.get(Uniform::AtlasSize), Size, Size);
        glUniform1i(stampUniforms_.get(Uniform::RadianceAtlas), 0);
        glUniform1i(stampUniforms_.get(Uniform::CoverageAtlas), 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlasLight_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, atlasCoverage_);
        glActiveTexture(GL_TEXTURE0);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, int(stamps_.size()));
        atlasStats_.stamps += int(stamps_.size());
        stamps_.clear();
    };
    auto flushDirect = [&]() {
        if (direct.empty())
            return;
        drawGridPlanes(direct, sx, sy, w, width, height, x, y);
        direct.clear();
    };
    for (size_t pi = 0; pi < planes.size(); ++pi) {
        const auto &p = planes[pi];
        if (selected[pi] < 0) {
            flushStamps();
            direct.push_back(p);
            ++atlasStats_.directPlanes;
            continue;
        }
        flushDirect();
        const auto &slot = slots[size_t(selected[pi])];
        for (int ix = p.firstX; ix <= p.lastX; ++ix)
            for (int iy = p.firstY; iy <= p.lastY; ++iy) {
                const float cx = float((p.x + ix * p.spacing + .5) * sx),
                            cy = float((p.y + iy * p.spacing + .5) * sy);
                for (auto r : slot.rects) {
                    if (cx + r.x1 < 0 || cx + r.x0 > width || cy + r.y1 < 0 || cy + r.y0 > height)
                        continue;
                    stamps_.push_back({cx + r.x0, cy + r.y0, cx + r.x1, cy + r.y1,
                                       slot.x + Pad + (r.x0 - slot.loX) * scale,
                                       slot.y + Pad + (r.y0 - slot.loY) * scale,
                                       slot.x + Pad + (r.x1 - slot.loX) * scale,
                                       slot.y + Pad + (r.y1 - slot.loY) * scale});
                    if (stamps_.size() == 8192)
                        flushStamps();
                }
            }
    }
    flushStamps();
    flushDirect();
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
}

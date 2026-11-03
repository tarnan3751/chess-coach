// A piece set: one QSvgRenderer per piece type, loaded from the app's Qt resource
// (:/pieces/<set>/<w|b><K|Q|R|B|N|P>.svg, data/pieces/), shared by every item that draws that
// piece (CLAUDE.md, **Qt** gotchas). Owned through Qt's parent: the renderers are its children.
#pragma once

#include <QObject>
#include <QPixmap>
#include <QString>
#include <array>

class QSvgRenderer;

namespace chesscoach::app {

class PieceSet : public QObject {
    Q_OBJECT

  public:
    explicit PieceSet(const QString& name, QObject* parent = nullptr);

    // The renderer of a piece given by its FEN letter (KQRBNP white, kqrbnp black), or nullptr
    // for any other character.
    [[nodiscard]] QSvgRenderer* renderer(char piece) const;

    // The piece drawn into a square pixmap of the given side, transparent around it.
    [[nodiscard]] QPixmap pixmap(char piece, int size) const;

    // Whether all twelve pieces loaded.
    [[nodiscard]] bool valid() const;

  private:
    std::array<QSvgRenderer*, 12> renderers_{};
};

} // namespace chesscoach::app

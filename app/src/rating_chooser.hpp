// The Rating chooser in the toolbar (docs/reference/app.md, **Shell**): a combo box whose arrow is
// the chevron-down icon in the muted colour (the palette's placeholder text), drawn over the
// combo the stylesheet draws, as a stylesheet cannot recolour an image. GUI thread only.
#pragma once

#include <QComboBox>
#include <QIcon>

class QEvent;
class QPaintEvent;

namespace chesscoach::app {

class RatingChooser : public QComboBox {
  public:
    using QComboBox::QComboBox;

  protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    QIcon chevron_; // made again when the palette or the style changes
};

} // namespace chesscoach::app

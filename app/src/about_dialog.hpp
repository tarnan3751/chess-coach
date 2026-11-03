// The About dialog (Help > About chess-coach): the app's version, the open database's data
// version, and the attributions of NOTICE in plain text, without links. GUI thread only.
#pragma once

#include <QDialog>
#include <QString>

namespace chesscoach::app {

class AboutDialog : public QDialog {
    Q_OBJECT

  public:
    // data_version is the open database's, or empty without one.
    explicit AboutDialog(const QString& data_version, QWidget* parent = nullptr);

    // The dialog's text.
    [[nodiscard]] static QString text(const QString& data_version);
};

} // namespace chesscoach::app

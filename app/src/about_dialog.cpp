// The About dialog (about_dialog.hpp).
#include "about_dialog.hpp"

#include <chesscoach/version.hpp>

#include <QDialogButtonBox>
#include <QLabel>
#include <QStringList>
#include <QVBoxLayout>

namespace chesscoach::app {

AboutDialog::AboutDialog(const QString& data_version, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("about_dialog"));
    setWindowTitle(tr("About chess-coach"));
    auto* label = new QLabel(text(data_version), this);
    label->setObjectName(QStringLiteral("about_text"));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* column = new QVBoxLayout(this);
    column->addWidget(label);
    column->addWidget(buttons);
}

QString AboutDialog::text(const QString& data_version) {
    const QStringList lines{
        tr("chess-coach %1").arg(QString::fromStdString(to_string(current_version()))),
        data_version.isEmpty() ? tr("No opening database is open.")
                               : tr("Opening database, data version %1").arg(data_version),
        QString(),
        tr("An opening coach that works offline. It is free software under the MIT licence."),
        QString(),
        tr("It is built on the work of others."),
        tr("Qt 6, GNU LGPL version 3, linked dynamically."),
        tr("chess-library by Disservin, MIT licence."),
        tr("The cburnett chess pieces by Colin M.L. Burnett, from Wikimedia Commons, CC BY-SA "
           "3.0."),
        tr("The Lichess game dumps, evaluation database and opening names, CC0."),
        tr("The fonts Source Serif 4 (version 4.005) and Source Sans 3 (version 3.052) by Adobe, "
           "SIL Open Font License 1.1."),
        tr("SQLite, public domain."),
        tr("{fmt}, spdlog, JSON for Modern C++, SQLiteC++, toml++ and reproc, MIT licence."),
        tr("CLI11 and Zstandard, BSD 3-Clause licence."),
        tr("tl::expected, CC0."),
        QString(),
        tr("The full notices are in the NOTICE file that comes with the app."),
    };
    return lines.join(QLatin1Char('\n'));
}

} // namespace chesscoach::app

#include "presence_for_plex/platform/qt/qt_dialog_manager.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QLineEdit>

namespace presence_for_plex::platform::qt {

QtDialogManager::QtDialogManager(QWidget* parent)
    : m_parent(parent) {
    PLEX_LOG_DEBUG(m_component_name, "QtDialogManager constructed");
}

QtDialogManager::~QtDialogManager() {
    PLEX_LOG_DEBUG(m_component_name, "QtDialogManager destructor called");
}

std::expected<DialogManager::DialogResult, UiError>
QtDialogManager::show_message(const std::string& title, const std::string& message, DialogType type) {
    QMessageBox::Icon icon;
    switch (type) {
        case DialogType::Info:
            icon = QMessageBox::Information;
            break;
        case DialogType::Warning:
            icon = QMessageBox::Warning;
            break;
        case DialogType::Error:
            icon = QMessageBox::Critical;
            break;
        case DialogType::Question:
            icon = QMessageBox::Question;
            break;
        default:
            icon = QMessageBox::NoIcon;
            break;
    }

    QMessageBox msgBox(icon, QString::fromStdString(title),
                       QString::fromStdString(message),
                       QMessageBox::Ok, m_parent);

    int result = msgBox.exec();

    if (result == QMessageBox::Ok) {
        return DialogResult::OK;
    }

    return DialogResult::Cancel;
}

std::expected<DialogManager::DialogResult, UiError>
QtDialogManager::show_question(const std::string& title, const std::string& question, bool show_cancel) {
    QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No;
    if (show_cancel) {
        buttons |= QMessageBox::Cancel;
    }

    QMessageBox msgBox(QMessageBox::Question,
                       QString::fromStdString(title),
                       QString::fromStdString(question),
                       buttons, m_parent);

    int result = msgBox.exec();

    switch (result) {
        case QMessageBox::Yes:
            return DialogResult::Yes;
        case QMessageBox::No:
            return DialogResult::No;
        case QMessageBox::Cancel:
            return DialogResult::Cancel;
        default:
            return DialogResult::Cancel;
    }
}

std::expected<std::string, UiError>
QtDialogManager::show_input_dialog(const std::string& title, const std::string& prompt,
                                   const std::string& default_value) {
    bool ok;
    QString text = QInputDialog::getText(m_parent,
                                         QString::fromStdString(title),
                                         QString::fromStdString(prompt),
                                         QLineEdit::Normal,
                                         QString::fromStdString(default_value),
                                         &ok);

    if (ok) {
        if (!text.isEmpty()) {
            return text.toStdString();
        }
        // User clicked OK but provided empty text - this is still valid input
        return text.toStdString();
    }

    // User cancelled the dialog (clicked Cancel or closed dialog)
    return std::unexpected(UiError::Cancelled);
}

std::expected<std::string, UiError>
QtDialogManager::show_password_dialog(const std::string& title, const std::string& prompt) {
    bool ok;
    QString text = QInputDialog::getText(m_parent,
                                         QString::fromStdString(title),
                                         QString::fromStdString(prompt),
                                         QLineEdit::Password,
                                         QString(),
                                         &ok);

    if (ok) {
        // User clicked OK - return whatever they entered (even if empty)
        return text.toStdString();
    }

    // User cancelled the dialog (clicked Cancel or closed dialog)
    return std::unexpected(UiError::Cancelled);
}

std::expected<std::string, UiError>
QtDialogManager::show_open_file_dialog(const std::string& title, const std::string& filter,
                                       const std::string& initial_dir) {
    QString fileName = QFileDialog::getOpenFileName(m_parent,
                                                    QString::fromStdString(title),
                                                    QString::fromStdString(initial_dir),
                                                    QString::fromStdString(filter));

    if (!fileName.isEmpty()) {
        return fileName.toStdString();
    }

    return std::unexpected(UiError::OperationFailed);
}

std::expected<std::string, UiError>
QtDialogManager::show_save_file_dialog(const std::string& title, const std::string& filter,
                                       const std::string& initial_dir, const std::string& default_name) {
    QString initial_path = QString::fromStdString(initial_dir);
    if (!default_name.empty()) {
        if (!initial_path.endsWith('/') && !initial_path.endsWith('\\')) {
            initial_path += '/';
        }
        initial_path += QString::fromStdString(default_name);
    }

    QString fileName = QFileDialog::getSaveFileName(m_parent,
                                                    QString::fromStdString(title),
                                                    initial_path,
                                                    QString::fromStdString(filter));

    if (!fileName.isEmpty()) {
        return fileName.toStdString();
    }

    return std::unexpected(UiError::OperationFailed);
}

std::expected<std::string, UiError>
QtDialogManager::show_folder_dialog(const std::string& title, const std::string& initial_dir) {
    QString dirName = QFileDialog::getExistingDirectory(m_parent,
                                                        QString::fromStdString(title),
                                                        QString::fromStdString(initial_dir),
                                                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dirName.isEmpty()) {
        return dirName.toStdString();
    }

    return std::unexpected(UiError::OperationFailed);
}

} // namespace presence_for_plex::platform::qt
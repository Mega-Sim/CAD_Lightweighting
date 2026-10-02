#include <QApplication>
#include <QFileDialog>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QMainWindow window;
    window.setWindowTitle("GigaRoute CAD Optimizer");
    window.resize(520, 160);

    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    auto* button = new QPushButton("Open DXF", central);
    auto* label = new QLabel("No file selected", central);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QObject::connect(button, &QPushButton::clicked, [&window, label]() {
        const auto file = QFileDialog::getOpenFileName(&window, "Open DXF", {}, "DXF files (*.dxf)");
        if (!file.isEmpty()) label->setText(file);
    });

    layout->addWidget(button);
    layout->addWidget(label);
    window.setCentralWidget(central);
    window.show();
    return app.exec();
}

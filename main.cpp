#include <iostream>

#include <ManapiInitTools.hpp>
#include <ManapiFetch2.hpp>
#include <ManapiEventLoop.hpp>

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>

#include "handlers.hpp"


class AppWindow : public QMainWindow {
public:
    AppWindow() {
        createFileMenu();
        resize(800, 600);
    }

private:
    QMenu *fileMenu;
    QAction *aboutAction;
    QAction *exitAction;

    void createFileMenu() {
        fileMenu = menuBar()->addMenu(tr("&File"));
        aboutAction = new QAction(tr("&About"), this);
        connect(aboutAction, &QAction::triggered, qApp, &QApplication::aboutQt);
        fileMenu->addAction(aboutAction);
        fileMenu->addSeparator();
        exitAction = new QAction(tr("E&xit"), this);
        exitAction->setShortcuts(QKeySequence::Quit);
        connect(exitAction, &QAction::triggered, this, &QWidget::close);
        fileMenu->addAction(exitAction);
    }
};

int main(int argc, char *argv[]) {
    manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_LOW);

    auto ctx = manapi::async::context::create(0).unwrap();
    auto router_ctx = manapi::net::http::server_ctx::create().unwrap();

    ctx->run ([argc, argv, router_ctx] (const std::function<void()>& cb) mutable -> void {
        auto eloop = new iz::Eventing::LibUvEventDispatcher();

        auto router = manapi::net::http::server::create(router_ctx).unwrap();
        router.GET("/", [] (manapi::net::http::request &req, manapi::net::http::uresponse resp)
            -> void {
            resp->text(std::format("{:}", manapi::time::current_time())).unwrap();
        });

        QApplication::setEventDispatcher(eloop);
        QApplication app (argc, argv);

        manapi::scope_ptr window (new QWidget);
        window->setWindowTitle("Динебот");
        manapi::scope_ptr<QLabel> label (new QLabel ("<center>Hello</center>"));
        manapi::scope_ptr<QPushButton> btn (new QPushButton("CLOSE"));
        label->setStyleSheet("QLabel { background-color : red; color : blue; }");
        manapi::scope_ptr<QVBoxLayout> vbox (new QVBoxLayout ());
        vbox->addWidget(label.release());
        vbox->addWidget(btn.release());

        manapi::async::run ([router] () mutable  -> manapi::future<> {
            manapi::unwrap(co_await router.config_object({
                {"pools", manapi::json::array({{
                    {"http", manapi::json::array({"1.1"})},
                    {"address", "127.0.0.1"},
                    {"port", "8885"}
                }})}
            }));
            manapi::unwrap(co_await router.start());
        });

        manapi::async::current()->timerpool()->append_interval_async (1000,manapi::TIMER_POOR, [label = label.get()] (const manapi::timer &) -> manapi::future<> {
            auto response = manapi::unwrap(co_await manapi::net::fetch2::fetch("http://127.0.0.1:8885"));
            auto text = manapi::unwrap(co_await response.text());
            std::cout << text << "\n";
            label->setText(QString::fromStdString(text));
        }).unwrap();

        window->setLayout(vbox.release());
        window->show();

        manapi::async::current()->eventloop()->custom_event_loop(
            [&app, eloop] () -> void {
            if (int rhs = app.exec()) {
                manapi_log_debug("app.exec() -> %d", rhs);
            }
            eloop->unsubscribe();
        });

        cb ();
    });

    return 0;
}
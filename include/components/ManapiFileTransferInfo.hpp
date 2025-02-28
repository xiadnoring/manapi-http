#pragma once

#include <string>

#include "../ManapiFilesystem.hpp"
#include "../ManapiHttpMime.hpp"


namespace manapi::net {
    class file_transfer_info {
    public:
        explicit file_transfer_info (std::string filelocal) {
            this->filelocal_ = std::move(filelocal);
            this->filename_ = filesystem::basename(this->filelocal_);
            this->filemime_ = mime_by_file_path(this->filename_);
        }

        file_transfer_info (std::string filelocal, std::string filename, std::string filemime) {
            this->filelocal_ = std::move(filelocal);
            this->filename_ = std::move(filename);
            this->filemime_ = std::move(filemime);
        }

        file_transfer_info (file_transfer_info &&n) noexcept {
            this->filelocal_ = std::move(n.filelocal_);
            this->filename_ = std::move(n.filename_);
            this->filemime_ = std::move(n.filemime_);
        }


        file_transfer_info &operator=(file_transfer_info &&n) noexcept {
            this->filelocal_ = std::move(n.filelocal_);
            this->filename_ = std::move(n.filename_);
            this->filemime_ = std::move(n.filemime_);
            return *this;
        }

        std::string &filelocal () {
            return this->filelocal_;
        }

        std::string &filename () {
            return this->filename_;
        }

        std::string &filemime () {
            return this->filemime_;
        }

    private:
        std::string filelocal_;
        std::string filename_;
        std::string filemime_;
    };
}
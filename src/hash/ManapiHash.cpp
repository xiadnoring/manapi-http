#include "hash/ManapiHash.hpp"

#include "fs/ManapiFilesystem.hpp"
#include "fs/ManapiFileStream.hpp"
#include "std/ManapiParallelRun.hpp"
#include "../include/ManapiUtils.hpp"

manapi::future<manapi::status> manapi::hash::hash_file(manapi::hash::hash_base *inst, manapi::ev::file src, int64_t offset, manapi::ctoken token) {
    auto st = manapi::ev::status_ok();
    manapi::unwrap(co_await manapi::async::eventloop()->wait_async_task ([&] ( const std::atomic<bool> &is_cancelled ) -> manapi::future<> {
        auto fin = manapi::fs::fstream::create (src, false).unwrap();
        fin->seekg( offset, manapi::fs::fstream::FILE_SEEK_START );

        auto read_sv = manapi::async::memory_fabric()->slice(manapi::object_pool::area_size() * 16).unwrap();

        while (true) {

            ssize_t rhs = co_await fin->read(read_sv);

            if (rhs < 0) {
                st = manapi::status_unknown("hash_file:read failed");
                co_return;
            }

            if (!rhs) break;

            auto read_sv_view = read_sv.subslice(0, static_cast<std::size_t>(rhs)).unwrap();

            for (const auto sv : read_sv_view) {
                inst->update(reinterpret_cast<const uint8_t *> (sv.data()), sv.size());
            }
        }
    }, std::move(token)));
    co_return std::move(st);
}

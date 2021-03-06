/*
    twain-server: This project exposes the TWAIN DSM as a web API
    Copyright (C) 2019 "Diagnósticos da América S.A. <bruno.f@dasa.com.br>"

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <nlohmann/json.hpp>
#include <list>

#include "external/twain.h"
#include "twain/transfer.hpp"
#include "twain/device.hpp"
#include "twain/dsm.hpp"
#include "twain/error_code.hpp"

#include <boost/asio.hpp>
#include <boost/asio/execution_context.hpp>

namespace dasa::gliese::scanner {

    /**
     * Class that interfaces with the TWAIN DSM
     *
     * The DSM is a FSM, so we replicate it here. States are all integers, in range [1; 7].
     * Please refer to the TWAIN DSM reference for more details
     */
    class Twain {
    public:
        explicit Twain(boost::asio::io_context &context);
        ~Twain() {
            shutdown();
        }

        boost::asio::io_context& get_io_context() {
            return context;
        }

        /**
         * Fills the application identity struct
         */
        void fillIdentity();

        /**
         * @brief Loads the DSM library from the specified path
         *
         * If the state is 2 or greater, this method does nothing
         * If the state is 1, loads the library and state goes to 2
         *
         * @remark On MacOS this method does nothing, since the DSM is statically linked
         * @param path The path to the library
         */
        void loadDSM(const char *path);

        /**
         * @brief Unloads the DSM library
         */
        void unloadDSM();

        /**
         * @brief Opens a connection to the DSM
         *
         * If the state is 3 or greater, this method does nothing
         * If the state is 1, abort() is called
         * If the state is 2, then the connection is opened and state goes to 3
         *
         * @remark If the connection fails, then the state continues to be 2
         */
        void openDSM();

        /**
         * @brief Closes the DSM connection
         */
        void closeDSM();

        /**
         * Returns the TWAIN state
         */
        int getState() { return state; }

        /**
         * Sets the new state of the TWAIN FSM
         */
        void setState(int newState);

        bool isUsingCallbacks() {
            return useCallbacks;
        }

        /**
         * Returns the application identity
         */
        pTW_IDENTITY getIdentity() {
            return &identity;
        }

        /**
         * Returns a list of all available TWAIN DS
         *
         * @throws std::error_code in case of failure
         */
        std::list<twain::Device> listSources();

        /**
         * Returns a list of all available TWAIN DS
         */
        std::list<twain::Device> listSources(std::error_code& ec) noexcept;

        /**
         * Fetches a list of all available DS asynchronously
         *
         * @param token Either `boost::asio::future` or a lambda that will be called when the list is fetched
         */
        template <typename CompletionToken>
        auto async_list_sources(CompletionToken&& token) {
            auto initiation = [](auto&& token, Twain& twain) {
                auto executor = boost::asio::get_associated_executor(token, twain.get_io_context());
                auto handler = [token, &twain]() {
                    std::error_code ec;
                    std::list<twain::Device> sources = twain.listSources(ec);
                    return token(ec, sources);
                };
                return boost::asio::post(boost::asio::bind_executor(executor, handler));
            };
            return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code, std::list<twain::Device>)>
                    (initiation, token, std::ref(*this));
        }

        /**
         * Returns the default TWAIN DS
         */
        TW_IDENTITY getDefaultDataSource();

        TW_STATUSUTF8 getStatus();

        /**
         * Open a connection to a DS
         * @param id The identifier of the DS
         * @return Whether the connection was successful
         */
        bool loadDataSource(dasa::gliese::scanner::twain::Device::TW_ID id);

        /**
         * Open a connection to a DS
         * @param device The identifier of the DS
         * @return Whether the connection was successful
         */
        bool loadDataSource(dasa::gliese::scanner::twain::Device &device);

        /**
         * Start acquiring images from a TWAIN DS
         * @param handle A handle to the main application window
         * @param showUI Whether to show the DS UI
         */
        void enableDataSource(TW_HANDLE handle, bool showUI);

        /**
         * Return the current loaded DS
         */
        dasa::gliese::scanner::twain::Device* getDataSource() { return currentDS.get(); }

        /**
         * Closes the connection to the current DS
         */
        bool closeDS();

        /**
         * Signal the DS that it will not be used anymore
         */
        void disableDS();

        TW_UINT16 setCapability(TW_UINT16 capability, int value, TW_UINT16 type);
		TW_UINT16 setCapability(TW_UINT16 Cap, const TW_FIX32* _pValue);
        TW_INT16 getCapability(TW_CAPABILITY& _cap, TW_UINT16 _msg = MSG_GET);
        bool resetAllCapabilities();

        std::weak_ptr<dasa::gliese::scanner::twain::Transfer> startScan(const std::string &outputMime);

        /**
         * Reset the TWAIN status.
         *
         * This efectivelly clear all transfers, disable and close the DS, close and unload the DSM
         * and then load and open the DSM
         */
        void reset();

        /**
         * Return the current DSM connection
         */
        twain::DSM& dsm() {
            return DSM;
        }

        /**
         * Forward call to DSM_Entry
         */
        TW_UINT16 operator()(pTW_IDENTITY pOrigin,
                             pTW_IDENTITY pDest,
                             TW_UINT32 DG,
                             TW_UINT16 DAT,
                             TW_UINT16 MSG,
                             TW_MEMREF pData) {
            return DSM(pOrigin, pDest, DG, DAT, MSG, pData);
        }

        /**
         * Forward call to DSM_Entry, using the application identity as origin and the current DS as destination
         */
        TW_UINT16 operator()(TW_UINT32 DG,
                             TW_UINT16 DAT,
                             TW_UINT16 MSG,
                             TW_MEMREF pData) {
            return DSM(getIdentity(), getDataSource() ? getDataSource()->getIdentity() : nullptr, DG, DAT, MSG, pData);
        }

        /**
         * Return the current active transfer
         */
        std::shared_ptr<dasa::gliese::scanner::twain::Transfer> getActiveTransfer() {
            return activeTransfer;
        }

        std::tuple<std::list<uint32_t>, std::list<uint32_t>> getDeviceDPIs(twain::Device::TW_ID device);

        /**
         * Close the active transfer
         */
        void endTransfer();

        void shutdown() noexcept;

    private:
        TW_IDENTITY identity{};
        TW_USERINTERFACE ui{};

        twain::DSM DSM;

        boost::asio::io_context& context;

        int state = 1;
        bool useCallbacks = false;

        std::unique_ptr<dasa::gliese::scanner::twain::Device> currentDS;
        std::shared_ptr<dasa::gliese::scanner::twain::Transfer> activeTransfer;

        std::unique_ptr<twain::Device> make_device(twain::Device::TW_ID id);

        static std::map<TW_UINT32, TW_MEMREF> map;

        bool getCurrent(TW_CAPABILITY *pCap, TW_UINT32& val);
    };

}

std::ostream& operator<<(std::ostream& os, const TW_IDENTITY& identity);

#include "server.h"

#include <SFML/Network/Packet.hpp>

#include <common/network/commands.h>
#include <common/network/input_state.h>

#include <ctime>
#include <iostream>
#include <random>
#include <thread>

namespace server {
    Server::Server(int maxConnections, Port port)
        : m_clientSessions(maxConnections)
        , m_clientStatuses(maxConnections)
    {
        std::fill(m_clientStatuses.begin(), m_clientStatuses.end(),
                  ClientStatus::Disconnected);

        m_socket.setBlocking(false);
        m_socket.bind(port);

        std::cout << "\n\nServer has started!" << std::endl;
        std::cout << "Listening for connections...\n" << std::endl;

        for (auto &entity : m_entities) {
            entity.alive = true;
        }
        m_entities[maxConnections + 1].alive = true;
        m_entities[maxConnections + 1].transform.position = {20, 1, 20};
        m_isRunning = true;
    }

    int Server::connectedPlayes() const
    {
        return m_connections;
    }

    int Server::maxConnections() const
    {
        return m_maxConnections;
    }

    int Server::findEmptySlot()
    {
        for (int i = 0; i < m_maxConnections; i++) {
            if (m_clientStatuses[i] == ClientStatus::Disconnected) {
                return i;
            }
        }
        return -1;
    }

    void Server::recievePackets()
    {
        PackagedCommand package;
        while (getFromClient(package)) {
            auto &packet = package.packet;
            switch (package.command) {
                case CommandToServer::PlayerInput:
                    handleKeyInput(packet);
                    break;

                case CommandToServer::Connect:
                    handleIncomingConnection(package.address, package.port);
                    break;

                case CommandToServer::Disconnect:
                    handleDisconnect(packet);
                    break;
            }
        }
    }

    void Server::updatePlayers()
    {
        for (int i = 0; i < m_maxConnections; i++) {
            if (m_clientStatuses[i] == ClientStatus::Connected) {
                auto &player = m_entities[i];
                auto input = m_clientSessions[i].keyState;
                auto isPressed = [input](PlayerInput key) {
                    return (input & key) == key;
                };

                if (isPressed(PlayerInput::Forwards)) {
                    player.moveForwards();
                }
                else if (isPressed(PlayerInput::Back)) {
                    player.moveBackwards();
                }
                if (isPressed(PlayerInput::Left)) {
                    player.moveLeft();
                }
                else if (isPressed(PlayerInput::Right)) {
                    player.moveRight();
                }
            }
        }
    }

    void Server::sendPackets()
    {
        sf::Packet statePacket;
        statePacket << CommandToClient::WorldState
                    << static_cast<u16>(m_entities.size());
        for (u16 entityId = 0; entityId < m_entities.size(); entityId++) {
            if (m_entities[entityId].alive) {
                auto &transform = m_entities[entityId].transform;
                statePacket << entityId << transform.position.x
                            << transform.position.y << transform.position.z
                            << transform.rotation.x << transform.rotation.y;
            }
        }
        sendToAllClients(statePacket);
    }

    void Server::handleKeyInput(sf::Packet &packet)
    {
        ClientId client;

        packet >> client;
        packet >> m_clientSessions[client].keyState;
        packet >> m_entities[client].transform.rotation.x;
        packet >> m_entities[client].transform.rotation.y;
    }

    bool Server::sendToClient(ClientId id, sf::Packet &packet)
    {
        if (m_clientStatuses[id] == ClientStatus::Connected) {
            return m_socket.send(packet, m_clientSessions[id].address,
                                 m_clientSessions[id].port) == sf::Socket::Done;
        }
        return false;
    }

    void Server::sendToAllClients(sf::Packet &packet)
    {
        for (int i = 0; i < m_maxConnections; i++) {
            sendToClient(i, packet);
        }
    }

    bool Server::getFromClient(PackagedCommand &package)
    {
        if (m_socket.receive(package.packet, package.address, package.port) ==
            sf::Socket::Done) {
            package.packet >> package.command;
            return true;
        }
        return false;
    }

    void Server::handleIncomingConnection(const sf::IpAddress &clientAddress,
                                          Port clientPort)
    {
        std::cout << "Connection request got\n";

        auto sendRejection = [this](ConnectionResult result,
                                    const sf::IpAddress &address, Port port) {
            auto rejectPacket =
                createCommandPacket(CommandToClient::ConnectRequestResult);
            rejectPacket << result;
            m_socket.send(rejectPacket, address, port);
        };

        // This makes sure there are not any duplicated connections
        for (const auto &endpoint : m_clientSessions) {
            if (clientAddress.toInteger() == endpoint.address.toInteger() &&
                clientPort == endpoint.port) {
                return;
            }
        }

        if (m_connections < m_maxConnections) {
            auto slot = findEmptySlot();
            if (slot < 0) {
                sendRejection(ConnectionResult::GameFull, clientAddress,
                              clientPort);
            }
            // Connection can be made
            sf::Packet responsePacket;
            responsePacket << CommandToClient::ConnectRequestResult
                           << ConnectionResult::Success
                           << static_cast<ClientId>(slot)
                           << static_cast<u8>(m_maxConnections);

            m_clientStatuses[slot] = ClientStatus::Connected;
            m_clientSessions[slot].address = clientAddress;
            m_clientSessions[slot].port = clientPort;
            m_entities[slot].transform.position = {10, 0, 10};
            m_entities[slot].alive = true;

            m_aliveEntities++;
            m_socket.send(responsePacket, clientAddress, clientPort);

            m_connections++;
            std::cout << "Client Connected slot: " << (int)slot << '\n';

            auto joinPack = createCommandPacket(CommandToClient::PlayerJoin);
            joinPack << static_cast<ClientId>(slot);
            sendToAllClients(joinPack);
        }
        else {
            sendRejection(ConnectionResult::GameFull, clientAddress,
                          clientPort);
        }
        std::cout << std::endl;
    }

    void Server::handleDisconnect(sf::Packet &packet)
    {
        ClientId client;
        packet >> client;
        m_clientStatuses[client] = ClientStatus::Disconnected;
        m_entities[client].alive = false;
        m_connections--;
        m_aliveEntities--;
        std::cout << "Client Disonnected slot: " << (int)client << '\n';
        std::cout << std::endl;

        auto joinPack = createCommandPacket(CommandToClient::PlayerLeave);
        joinPack << client;
        sendToAllClients(joinPack);
    }

} // namespace server

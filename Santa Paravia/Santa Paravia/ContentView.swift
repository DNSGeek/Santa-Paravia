import SwiftUI
import Combine

// Swift wrapper around the C player struct
class GameEngine: ObservableObject {
    // We need to manually trigger updates since C structs don't auto-publish
    @Published var updateTrigger = 0
    
    var player: player
    
    init() {
        // Initialize the C player struct
        player = Player()
        
        // Initialize using C function
        var name = "Giovanni"
        name.withCString { namePtr in
            let mutablePtr = UnsafeMutablePointer(mutating: namePtr)
            InitializePlayer(&player, 1400, 0, 2, mutablePtr, 1) // 1 = male
        }
    }
    
    func nextTurn() {
        GenerateHarvest(&player)
        NewLandAndGrainPrices(&player)
        updateTrigger += 1
    }
    
    func buyMarketplace() {
        if player.Treasury >= 1000 {
            BuyMarket(&player)
            updateTrigger += 1
        }
    }
    
    func buyMill() {
        if player.Treasury >= 2000 {
            BuyMill(&player)
            updateTrigger += 1
        }
    }
    
    func buyPalace() {
        if player.Treasury >= 3000 {
            BuyPalace(&player)
            updateTrigger += 1
        }
    }
    
    func buyCathedral() {
        if player.Treasury >= 5000 {
            BuyCathedral(&player)
            updateTrigger += 1
        }
    }
    
    func buySoldiers() {
        if player.Treasury >= 500 && player.Serfs >= 20 {
            BuySoldiers(&player)
            updateTrigger += 1
        }
    }
    
    // Helper to get title as String
    var titleString: String {
        withUnsafeBytes(of: player.Title) { buffer in
            let data = Data(buffer)
            if let string = String(data: data, encoding: .utf8) {
                return string.trimmingCharacters(in: .controlCharacters)
            }
            return "Unknown"
        }
    }
}

struct ContentView: View {
    @StateObject private var game = GameEngine()
    
    var body: some View {
        ScrollView {
            VStack(spacing: 20) {
                // Header
                Text("Santa Paravia and Fiumaccio")
                    .font(.largeTitle)
                    .bold()
                
                Text(game.titleString)
                    .font(.title2)
                
                Text("Year \(game.player.Year)")
                    .font(.title3)
                
                Divider()
                
                // Main Stats
                VStack(alignment: .leading, spacing: 15) {
                    StatRow(label: "Treasury", value: "\(game.player.Treasury) florins")
                    StatRow(label: "Grain Reserve", value: "\(game.player.GrainReserve) steres")
                    StatRow(label: "Land", value: "\(game.player.Land) hectares")
                    StatRow(label: "Serfs", value: "\(game.player.Serfs)")
                    StatRow(label: "Soldiers", value: "\(game.player.Soldiers)")
                }
                .padding()
                .background(Color.gray.opacity(0.1))
                .cornerRadius(10)
                
                // Population Stats
                VStack(alignment: .leading, spacing: 10) {
                    Text("Population")
                        .font(.headline)
                    StatRow(label: "Nobles", value: "\(game.player.Nobles)")
                    StatRow(label: "Clergy", value: "\(game.player.Clergy)")
                    StatRow(label: "Merchants", value: "\(game.player.Merchants)")
                }
                .padding()
                .background(Color.blue.opacity(0.1))
                .cornerRadius(10)
                
                // Buildings
                VStack(alignment: .leading, spacing: 10) {
                    Text("Buildings")
                        .font(.headline)
                    StatRow(label: "Marketplaces", value: "\(game.player.Marketplaces)")
                    StatRow(label: "Mills", value: "\(game.player.Mills)")
                    StatRow(label: "Palace", value: "\(game.player.Palace)")
                    StatRow(label: "Cathedral", value: "\(game.player.Cathedral)")
                }
                .padding()
                .background(Color.green.opacity(0.1))
                .cornerRadius(10)
                
                // Purchase Buttons
                VStack(spacing: 10) {
                    Text("State Purchases")
                        .font(.headline)
                    
                    PurchaseButton(
                        title: "Marketplace",
                        cost: 1000,
                        canAfford: game.player.Treasury >= 1000,
                        action: game.buyMarketplace
                    )
                    
                    PurchaseButton(
                        title: "Woolen Mill",
                        cost: 2000,
                        canAfford: game.player.Treasury >= 2000,
                        action: game.buyMill
                    )
                    
                    PurchaseButton(
                        title: "Palace",
                        cost: 3000,
                        canAfford: game.player.Treasury >= 3000,
                        action: game.buyPalace
                    )
                    
                    PurchaseButton(
                        title: "Cathedral",
                        cost: 5000,
                        canAfford: game.player.Treasury >= 5000,
                        action: game.buyCathedral
                    )
                    
                    PurchaseButton(
                        title: "Soldiers (20)",
                        cost: 500,
                        canAfford: game.player.Treasury >= 500 && game.player.Serfs >= 20,
                        action: game.buySoldiers
                    )
                }
                .padding()
                
                // Next Turn Button
                Button(action: {
                    game.nextTurn()
                }) {
                    Text("Next Turn")
                        .font(.title2)
                        .padding()
                        .frame(maxWidth: .infinity)
                        .background(Color.blue)
                        .foregroundColor(.white)
                        .cornerRadius(10)
                }
                .padding(.horizontal)
            }
            .padding()
            .id(game.updateTrigger) // Force view refresh when updateTrigger changes
        }
    }
}

// Reusable stat row
struct StatRow: View {
    let label: String
    let value: String
    
    var body: some View {
        HStack {
            Text(label)
                .font(.body)
            Spacer()
            Text(value)
                .font(.body)
                .foregroundColor(.secondary)
        }
    }
}

// Purchase button component
struct PurchaseButton: View {
    let title: String
    let cost: Int
    let canAfford: Bool
    let action: () -> Void
    
    var body: some View {
        Button(action: action) {
            HStack {
                Text(title)
                Spacer()
                Text("\(cost) florins")
            }
            .padding()
            .background(canAfford ? Color.green.opacity(0.2) : Color.gray.opacity(0.2))
            .foregroundColor(canAfford ? .primary : .secondary)
            .cornerRadius(8)
        }
        .disabled(!canAfford)
    }
}

#Preview {
    ContentView()
}

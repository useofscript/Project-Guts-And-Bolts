-- A sample Studio plugin: gives every selected part a random bright colour.
-- Plugins are Lua files in the "plugins" folder next to Studio. They get the
-- same things scripts do (workspace, Instance.new, Color3...) plus `plugin`
-- (to add buttons to the PLUGINS tab) and `Selection` (what you've picked).

plugin:Button("Random Colors", "Give the selected parts random bright colours", function()
    local count = 0
    for _, thing in ipairs(Selection:Get()) do
        local parts = { thing }
        if not thing:IsA("BasePart") then parts = thing:GetDescendants() end
        for _, part in ipairs(parts) do
            if part:IsA("BasePart") then
                part.Color = Color3.fromHSV(math.random(), 0.7, 1)
                count = count + 1
            end
        end
    end
    print("Random Colors: painted " .. count .. " part(s)")
end)

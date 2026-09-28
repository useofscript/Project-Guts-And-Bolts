-- A sample Studio plugin: stacks copies of the selected part into a tower.

plugin:Button("Stack x5", "Stack 5 copies of the selected part on top of it", function()
    local picked = Selection:Get()
    local part = picked[1]
    if part == nil or not part:IsA("BasePart") then
        print("Stack Tower: select one part first")
        return
    end
    local made = { part }
    local last = part
    for i = 1, 5 do
        local copy = last:Clone()
        copy.Parent = last.Parent
        copy.Position = last.Position + Vector3.new(0, last.Size.Y, 0)
        table.insert(made, copy)
        last = copy
    end
    Selection:Set(made)
    print("Stack Tower: stacked 5 copies")
end)

plugin:Button("Ground It", "Move the selected parts down onto the ground (y = 0)", function()
    for _, part in ipairs(Selection:Get()) do
        if part:IsA("BasePart") then
            part.Position = Vector3.new(part.Position.X, part.Size.Y / 2, part.Position.Z)
        end
    end
end)
